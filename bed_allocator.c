#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================
 * project : hospital patient triage and bed allocator
 * file    : admissions.c
 * group   : group xx
 * members : Muhammad Talha (23F-0511), Abdul Rafay (23F-0591),
 *           Masooma Mirza (23F-0876)
 * purpose : central admissions manager with ipc, threads,
 *           scheduling, semaphores, fork/exec, and memory allocation
 * compile : gcc -Wall -Wextra -pthread
 * ============================================================
 */

#include "bed_allocator.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define mmap_log_size 65536
#define read_buffer_size 256
#define line_buffer_size 512

typedef struct {
    PatientRecord items[patient_queue_capacity];
    int count;
    pthread_mutex_t lock;
} PatientQueue;

typedef struct {
    int patient_ids[nurse_queue_capacity];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    char bed_type[max_bed_type_len];
} NurseQueue;

typedef struct {
    const char *name;
    int arrival;
    int burst;
    int priority;
    int done;
} SimJob;

static volatile sig_atomic_t keep_running = 1;

static SharedWardState *ward = NULL;
static int shm_fd = -1;
static AllocationStrategy current_strategy = allocation_best;

static pthread_mutex_t bed_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bed_freed_cond = PTHREAD_COND_INITIALIZER;

static PatientQueue patient_queue;
static sem_t queue_slots;
static sem_t queue_items;

static sem_t *icu_limit_sem = SEM_FAILED;
static sem_t *isolation_limit_sem = SEM_FAILED;

static NurseQueue nurse_queues[3];
static ActivePatient active_patients[max_active_patients];
static pthread_mutex_t active_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mmap_log_lock = PTHREAD_MUTEX_INITIALIZER;

static double total_waiting_time = 0.0;
static double total_turnaround_time = 0.0;
static int completed_patients = 0;
static int next_patient_id = 1001;

static int mmap_log_fd = -1;
static char *mmap_log_region = NULL;
static size_t mmap_log_offset = 0;

static void sleep_millis(long milliseconds)
{
    struct timespec request;
    struct timespec remaining;

    request.tv_sec = milliseconds / 1000;
    request.tv_nsec = (milliseconds % 1000) * 1000000L;

    while (nanosleep(&request, &remaining) == -1 && errno == EINTR) {
        request = remaining;
    }
}

static void make_timeout(struct timespec *timeout, int seconds)
{
    clock_gettime(CLOCK_REALTIME, timeout);
    timeout->tv_sec += seconds;
}

static void safe_printf(const char *format, ...)
{
    va_list args;

    pthread_mutex_lock(&print_lock);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    pthread_mutex_unlock(&print_lock);
}

static void append_schedule_log(const char *format, ...)
{
    FILE *file;
    va_list args;
    time_t now;
    struct tm *local_time;
    char time_text[64];

    pthread_mutex_lock(&log_lock);

    file = fopen(schedule_log_path, "a");
    if (file == NULL) {
        pthread_mutex_unlock(&log_lock);
        return;
    }

    now = time(NULL);
    local_time = localtime(&now);

    if (local_time != NULL) {
        strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", local_time);
    } else {
        snprintf(time_text, sizeof(time_text), "unknown-time");
    }

    fprintf(file, "[%s] ", time_text);
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fprintf(file, "\n");

    fclose(file);
    pthread_mutex_unlock(&log_lock);
}

static void init_mmap_log(void)
{
    mmap_log_fd = open(patient_records_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (mmap_log_fd == -1) {
        return;
    }

    if (ftruncate(mmap_log_fd, mmap_log_size) == -1) {
        close(mmap_log_fd);
        mmap_log_fd = -1;
        return;
    }

    mmap_log_region = mmap(NULL, mmap_log_size, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_log_fd, 0);
    if (mmap_log_region == MAP_FAILED) {
        mmap_log_region = NULL;
        close(mmap_log_fd);
        mmap_log_fd = -1;
        return;
    }
}

static void append_mmap_record(const char *format, ...)
{
    char line[256];
    va_list args;
    size_t length;

    if (mmap_log_region == NULL) {
        return;
    }

    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    length = strlen(line);

    pthread_mutex_lock(&mmap_log_lock);

    if (mmap_log_offset + length + 1 < mmap_log_size) {
        memcpy(mmap_log_region + mmap_log_offset, line, length);
        mmap_log_offset += length;
        mmap_log_region[mmap_log_offset] = '\n';
        mmap_log_offset++;
    }

    pthread_mutex_unlock(&mmap_log_lock);
}

static void close_mmap_log(void)
{
    if (mmap_log_region != NULL) {
        msync(mmap_log_region, mmap_log_size, MS_SYNC);
        munmap(mmap_log_region, mmap_log_size);
        mmap_log_region = NULL;
    }

    if (mmap_log_fd != -1) {
        close(mmap_log_fd);
        mmap_log_fd = -1;
    }
}

static int parse_int_value(const char *text, int *value_out)
{
    char *end = NULL;
    long value;

    if (text == NULL || value_out == NULL) {
        return -1;
    }

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *value_out = (int)value;
    return 0;
}

static void handle_term_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static void handle_sigchld(int signal_number)
{
    int saved_errno = errno;

    (void)signal_number;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }

    errno = saved_errno;
}

static void setup_signal_handlers(void)
{
    struct sigaction action;
    struct sigaction child_action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_term_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    memset(&child_action, 0, sizeof(child_action));
    child_action.sa_handler = handle_sigchld;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &child_action, NULL);
}

static int make_fifo_if_needed(const char *path)
{
    if (mkfifo(path, 0666) == -1 && errno != EEXIST) {
        fprintf(stderr, "could not create fifo %s: %s\n", path, strerror(errno));
        return -1;
    }

    return 0;
}

static int init_shared_ward(void)
{
    shm_unlink(shm_name);

    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        fprintf(stderr, "shared memory open failed: %s\n", strerror(errno));
        return -1;
    }

    if (ftruncate(shm_fd, sizeof(SharedWardState)) == -1) {
        fprintf(stderr, "shared memory size failed: %s\n", strerror(errno));
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    ward = mmap(NULL, sizeof(SharedWardState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ward == MAP_FAILED) {
        fprintf(stderr, "shared memory attach failed: %s\n", strerror(errno));
        ward = NULL;
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    init_ward_state(ward);
    log_memory_event("system-start", ward);

    return 0;
}

static int init_named_semaphores(void)
{
    sem_unlink(sem_icu_limit_name);
    sem_unlink(sem_isolation_limit_name);

    icu_limit_sem = sem_open(sem_icu_limit_name, O_CREAT | O_EXCL, 0666, icu_capacity);
    if (icu_limit_sem == SEM_FAILED) {
        fprintf(stderr, "icu semaphore open failed: %s\n", strerror(errno));
        return -1;
    }

    isolation_limit_sem = sem_open(sem_isolation_limit_name, O_CREAT | O_EXCL, 0666, isolation_capacity);
    if (isolation_limit_sem == SEM_FAILED) {
        fprintf(stderr, "isolation semaphore open failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void cleanup_ipc(void)
{
    close_mmap_log();

    if (ward != NULL) {
        munmap(ward, sizeof(SharedWardState));
        ward = NULL;
    }

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

    shm_unlink(shm_name);

    if (icu_limit_sem != SEM_FAILED) {
        sem_close(icu_limit_sem);
        sem_unlink(sem_icu_limit_name);
        icu_limit_sem = SEM_FAILED;
    }

    if (isolation_limit_sem != SEM_FAILED) {
        sem_close(isolation_limit_sem);
        sem_unlink(sem_isolation_limit_name);
        isolation_limit_sem = SEM_FAILED;
    }

    unlink(triage_fifo_path);
    unlink(discharge_fifo_path);
    unlink(pid_file_path);
}

static void init_patient_queue(void)
{
    patient_queue.count = 0;
    pthread_mutex_init(&patient_queue.lock, NULL);
    sem_init(&queue_slots, 0, patient_queue_capacity);
    sem_init(&queue_items, 0, 0);
}

static void destroy_patient_queue(void)
{
    sem_destroy(&queue_slots);
    sem_destroy(&queue_items);
    pthread_mutex_destroy(&patient_queue.lock);
}

static void init_nurse_queues(void)
{
    int i;

    for (i = 0; i < 3; i++) {
        nurse_queues[i].head = 0;
        nurse_queues[i].tail = 0;
        nurse_queues[i].count = 0;
        pthread_mutex_init(&nurse_queues[i].lock, NULL);
        pthread_cond_init(&nurse_queues[i].cond, NULL);
    }

    snprintf(nurse_queues[0].bed_type, sizeof(nurse_queues[0].bed_type), "%s", bed_type_icu);
    snprintf(nurse_queues[1].bed_type, sizeof(nurse_queues[1].bed_type), "%s", bed_type_isolation);
    snprintf(nurse_queues[2].bed_type, sizeof(nurse_queues[2].bed_type), "%s", bed_type_general);
}

static void destroy_nurse_queues(void)
{
    int i;

    for (i = 0; i < 3; i++) {
        pthread_mutex_destroy(&nurse_queues[i].lock);
        pthread_cond_destroy(&nurse_queues[i].cond);
    }
}

static int nurse_index_from_bed_type(const char *bed_type)
{
    if (bed_type != NULL && strcmp(bed_type, bed_type_icu) == 0) {
        return 0;
    }

    if (bed_type != NULL && strcmp(bed_type, bed_type_isolation) == 0) {
        return 1;
    }

    return 2;
}

static int wait_for_sem_timed(sem_t *sem_object)
{
    struct timespec timeout;
    int result;

    while (keep_running) {
        make_timeout(&timeout, 1);
        result = sem_timedwait(sem_object, &timeout);

        if (result == 0) {
            return 0;
        }

        if (errno == ETIMEDOUT || errno == EINTR) {
            continue;
        }

        return -1;
    }

    return -1;
}

static int wait_for_named_sem_timed(sem_t *sem_object, const char *name)
{
    struct timespec timeout;
    int result;

    append_schedule_log("waiting on %s semaphore", name);

    while (keep_running) {
        make_timeout(&timeout, 1);
        result = sem_timedwait(sem_object, &timeout);

        if (result == 0) {
            append_schedule_log("acquired %s semaphore", name);
            return 0;
        }

        if (errno == ETIMEDOUT || errno == EINTR) {
            continue;
        }

        append_schedule_log("failed on %s semaphore: %s", name, strerror(errno));
        return -1;
    }

    return -1;
}

static void patient_queue_insert_sorted(const PatientRecord *patient)
{
    int position;
    int i;

    pthread_mutex_lock(&patient_queue.lock);

    if (patient_queue.count >= patient_queue_capacity) {
        pthread_mutex_unlock(&patient_queue.lock);
        return;
    }

    position = patient_queue.count;
    for (i = 0; i < patient_queue.count; i++) {
        if (patient->priority < patient_queue.items[i].priority) {
            position = i;
            break;
        }
    }

    for (i = patient_queue.count; i > position; i--) {
        patient_queue.items[i] = patient_queue.items[i - 1];
    }

    patient_queue.items[position] = *patient;
    patient_queue.count++;

    pthread_mutex_unlock(&patient_queue.lock);
}

static int patient_queue_pop(PatientRecord *patient_out)
{
    int i;

    if (patient_out == NULL) {
        return -1;
    }

    pthread_mutex_lock(&patient_queue.lock);

    if (patient_queue.count <= 0) {
        pthread_mutex_unlock(&patient_queue.lock);
        return -1;
    }

    *patient_out = patient_queue.items[0];

    for (i = 0; i < patient_queue.count - 1; i++) {
        patient_queue.items[i] = patient_queue.items[i + 1];
    }

    patient_queue.count--;
    pthread_mutex_unlock(&patient_queue.lock);

    return 0;
}

static int parse_patient_line(const char *line, PatientRecord *patient)
{
    char local_line[line_buffer_size];
    char *fields[6];
    char *token;
    char *save_ptr = NULL;
    int field_count = 0;
    int age;
    int severity;
    int priority;
    int care_units;
    int infectious = 0;

    if (line == NULL || patient == NULL) {
        return -1;
    }

    if (line[0] == '\0') {
        return -1;
    }

    snprintf(local_line, sizeof(local_line), "%s", line);

    token = strtok_r(local_line, "|", &save_ptr);
    while (token != NULL && field_count < 6) {
        fields[field_count] = token;
        field_count++;
        token = strtok_r(NULL, "|", &save_ptr);
    }

    if (field_count < 5) {
        return -1;
    }

    if (parse_int_value(fields[1], &age) == -1 || parse_int_value(fields[2], &severity) == -1 ||
        parse_int_value(fields[3], &priority) == -1 || parse_int_value(fields[4], &care_units) == -1) {
        return -1;
    }

    if (field_count >= 6) {
        parse_int_value(fields[5], &infectious);
    }

    memset(patient, 0, sizeof(*patient));
    patient->patient_id = next_patient_id++;
    snprintf(patient->name, sizeof(patient->name), "%s", fields[0]);
    patient->age = age;
    patient->severity = severity;
    patient->priority = priority;
    patient->care_units = care_units;
    patient->arrival_time = time(NULL);

    if (infectious && patient->care_units < isolation_units) {
        patient->care_units = isolation_units;
    }

    return 0;
}

static void enqueue_patient(const PatientRecord *patient)
{
    if (wait_for_sem_timed(&queue_slots) == -1) {
        return;
    }

    patient_queue_insert_sorted(patient);
    sem_post(&queue_items);

    safe_printf("receptionist: patient %d (%s) queued | severity=%d | priority=%d | care_units=%d\n",
                patient->patient_id,
                patient->name,
                patient->severity,
                patient->priority,
                patient->care_units);

    append_schedule_log("queued patient=%d name=%s priority=%d care_units=%d", patient->patient_id, patient->name, patient->priority, patient->care_units);
}

static void handle_patient_line(const char *line)
{
    PatientRecord patient;

    if (strcmp(line, "shutdown") == 0) {
        keep_running = 0;
        return;
    }

    if (parse_patient_line(line, &patient) == -1) {
        append_schedule_log("ignored invalid patient line: %s", line);
        return;
    }

    enqueue_patient(&patient);
}

static void *receptionist_thread(void *arg)
{
    int fifo_fd;
    char read_buffer[read_buffer_size];
    char line_buffer[line_buffer_size];
    size_t line_length = 0;

    (void)arg;

    fifo_fd = open(triage_fifo_path, O_RDWR | O_NONBLOCK);
    if (fifo_fd == -1) {
        safe_printf("receptionist: could not open triage fifo: %s\n", strerror(errno));
        return NULL;
    }

    safe_printf("receptionist: listening on %s\n", triage_fifo_path);

    while (keep_running) {
        ssize_t bytes_read = read(fifo_fd, read_buffer, sizeof(read_buffer));

        if (bytes_read > 0) {
            ssize_t i;

            for (i = 0; i < bytes_read; i++) {
                char character = read_buffer[i];

                if (character == '\n') {
                    line_buffer[line_length] = '\0';
                    handle_patient_line(line_buffer);
                    line_length = 0;
                } else if (line_length < sizeof(line_buffer) - 1) {
                    line_buffer[line_length] = character;
                    line_length++;
                }
            }
        } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            safe_printf("receptionist: fifo read error: %s\n", strerror(errno));
            break;
        } else {
            sleep_millis(100);
        }
    }

    close(fifo_fd);
    safe_printf("receptionist: stopped\n");
    return NULL;
}

static int add_active_patient(const PatientRecord *patient, pid_t pid, int partition_id, const char *bed_type)
{
    int i;

    pthread_mutex_lock(&active_lock);

    for (i = 0; i < max_active_patients; i++) {
        if (!active_patients[i].is_active) {
            active_patients[i].patient_id = patient->patient_id;
            active_patients[i].pid = pid;
            active_patients[i].partition_id = partition_id;
            active_patients[i].priority = patient->priority;
            active_patients[i].care_units = patient->care_units;
            active_patients[i].arrival_time = patient->arrival_time;
            active_patients[i].start_time = time(NULL);
            active_patients[i].is_active = 1;
            snprintf(active_patients[i].bed_type, sizeof(active_patients[i].bed_type), "%s", bed_type);
            pthread_mutex_unlock(&active_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&active_lock);
    return -1;
}

static int get_active_patient(int patient_id, ActivePatient *patient_out)
{
    int i;

    pthread_mutex_lock(&active_lock);

    for (i = 0; i < max_active_patients; i++) {
        if (active_patients[i].is_active && active_patients[i].patient_id == patient_id) {
            if (patient_out != NULL) {
                *patient_out = active_patients[i];
            }
            pthread_mutex_unlock(&active_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&active_lock);
    return -1;
}

static int remove_active_patient(int patient_id, ActivePatient *patient_out)
{
    int i;

    pthread_mutex_lock(&active_lock);

    for (i = 0; i < max_active_patients; i++) {
        if (active_patients[i].is_active && active_patients[i].patient_id == patient_id) {
            if (patient_out != NULL) {
                *patient_out = active_patients[i];
            }
            memset(&active_patients[i], 0, sizeof(active_patients[i]));
            pthread_mutex_unlock(&active_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&active_lock);
    return -1;
}

static void release_capacity_for_bed_type(const char *bed_type)
{
    if (bed_type != NULL && strcmp(bed_type, bed_type_icu) == 0 && icu_limit_sem != SEM_FAILED) {
        sem_post(icu_limit_sem);
        append_schedule_log("released icu semaphore");
    } else if (bed_type != NULL && strcmp(bed_type, bed_type_isolation) == 0 && isolation_limit_sem != SEM_FAILED) {
        sem_post(isolation_limit_sem);
        append_schedule_log("released isolation semaphore");
    }
}

static int acquire_capacity_for_bed_type(const char *bed_type)
{
    if (bed_type != NULL && strcmp(bed_type, bed_type_icu) == 0) {
        return wait_for_named_sem_timed(icu_limit_sem, "icu limit");
    }

    if (bed_type != NULL && strcmp(bed_type, bed_type_isolation) == 0) {
        return wait_for_named_sem_timed(isolation_limit_sem, "isolation limit");
    }

    return 0;
}

static int start_patient_process(const PatientRecord *patient, int partition_id, const char *bed_type)
{
    pid_t pid;
    char patient_id_text[32];
    char priority_text[32];
    char partition_id_text[32];
    char care_units_text[32];

    snprintf(patient_id_text, sizeof(patient_id_text), "%d", patient->patient_id);
    snprintf(priority_text, sizeof(priority_text), "%d", patient->priority);
    snprintf(partition_id_text, sizeof(partition_id_text), "%d", partition_id);
    snprintf(care_units_text, sizeof(care_units_text), "%d", patient->care_units);

    pid = fork();

    if (pid == -1) {
        safe_printf("scheduler: fork failed for patient %d: %s\n", patient->patient_id, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        char *child_args[] = {
            "./build/patient_simulator",
            patient_id_text,
            priority_text,
            partition_id_text,
            (char *)bed_type,
            care_units_text,
            NULL
        };

        execv(child_args[0], child_args);
        fprintf(stderr, "execv failed: %s\n", strerror(errno));
        _exit(127);
    }

    if (add_active_patient(patient, pid, partition_id, bed_type) == -1) {
        safe_printf("scheduler: active table full for patient %d\n", patient->patient_id);
    }

    append_mmap_record("admit patient=%d name=%s priority=%d bed=%d type=%s pid=%ld",
                       patient->patient_id,
                       patient->name,
                       patient->priority,
                       partition_id,
                       bed_type,
                       (long)pid);

    return 0;
}

static void log_patient_admission(const PatientRecord *patient, int partition_id, const char *bed_type)
{
    int wasted_units = internal_fragmentation_units(patient->care_units);

    safe_printf("scheduler: admitted patient %d (%s) to partition %d type=%s using %s\n",
                patient->patient_id,
                patient->name,
                partition_id,
                bed_type,
                allocation_strategy_name(current_strategy));

    safe_printf("paging: patient %d care_units=%d page_size=%d internal_fragmentation=%d units\n",
                patient->patient_id,
                patient->care_units,
                page_size_units,
                wasted_units);

    append_schedule_log("admitted patient=%d priority=%d partition=%d type=%s waiting_time=%ld",
                        patient->patient_id,
                        patient->priority,
                        partition_id,
                        bed_type,
                        (long)(time(NULL) - patient->arrival_time));
}

static int allocate_with_wait(const PatientRecord *patient, int *partition_id_out)
{
    const char *bed_type = bed_type_for_patient(patient);
    int capacity_acquired = 0;

    while (keep_running) {
        if (!capacity_acquired) {
            if (acquire_capacity_for_bed_type(bed_type) == -1) {
                return -1;
            }
            capacity_acquired = 1;
        }

        pthread_mutex_lock(&bed_lock);

        if (allocate_bed(ward, patient, current_strategy, partition_id_out) == 0) {
            log_memory_event("allocation", ward);
            pthread_mutex_unlock(&bed_lock);
            return 0;
        }

        pthread_mutex_unlock(&bed_lock);
        release_capacity_for_bed_type(bed_type);
        capacity_acquired = 0;

        append_schedule_log("no suitable bed for patient=%d priority=%d; scheduler waiting", patient->patient_id, patient->priority);
        safe_printf("scheduler: no suitable bed for patient %d, waiting for bed_freed condition\n", patient->patient_id);

        pthread_mutex_lock(&bed_lock);
        while (keep_running) {
            struct timespec timeout;
            make_timeout(&timeout, 1);
            if (pthread_cond_timedwait(&bed_freed_cond, &bed_lock, &timeout) == ETIMEDOUT) {
                break;
            }
            break;
        }
        pthread_mutex_unlock(&bed_lock);
    }

    if (capacity_acquired) {
        release_capacity_for_bed_type(bed_type);
    }

    return -1;
}

static void *scheduler_thread(void *arg)
{
    (void)arg;

    safe_printf("scheduler: started with %s strategy\n", allocation_strategy_name(current_strategy));

    while (keep_running) {
        PatientRecord patient;
        int partition_id = -1;
        const char *bed_type;

        if (wait_for_sem_timed(&queue_items) == -1) {
            continue;
        }

        if (!keep_running) {
            break;
        }

        if (patient_queue_pop(&patient) == -1) {
            continue;
        }

        sem_post(&queue_slots);

        bed_type = bed_type_for_patient(&patient);

        if (allocate_with_wait(&patient, &partition_id) == -1) {
            append_schedule_log("admission cancelled for patient=%d", patient.patient_id);
            continue;
        }

        pthread_mutex_lock(&bed_lock);
        safe_printf("scheduler: ward after allocation for patient %d\n", patient.patient_id);
        print_ward_map(stdout, ward);
        pthread_mutex_unlock(&bed_lock);

        if (start_patient_process(&patient, partition_id, bed_type) == -1) {
            pthread_mutex_lock(&bed_lock);
            free_bed(ward, patient.patient_id);
            pthread_cond_broadcast(&bed_freed_cond);
            pthread_mutex_unlock(&bed_lock);
            release_capacity_for_bed_type(bed_type);
            continue;
        }

        log_patient_admission(&patient, partition_id, bed_type);
    }

    safe_printf("scheduler: stopped\n");
    return NULL;
}

static int push_nurse_event(int nurse_index, int patient_id)
{
    NurseQueue *queue;

    if (nurse_index < 0 || nurse_index >= 3) {
        return -1;
    }

    queue = &nurse_queues[nurse_index];

    pthread_mutex_lock(&queue->lock);

    if (queue->count >= nurse_queue_capacity) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    queue->patient_ids[queue->tail] = patient_id;
    queue->tail = (queue->tail + 1) % nurse_queue_capacity;
    queue->count++;
    pthread_cond_signal(&queue->cond);

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

static int pop_nurse_event(int nurse_index, int *patient_id_out)
{
    NurseQueue *queue;

    if (nurse_index < 0 || nurse_index >= 3 || patient_id_out == NULL) {
        return -1;
    }

    queue = &nurse_queues[nurse_index];
    pthread_mutex_lock(&queue->lock);

    while (queue->count == 0 && keep_running) {
        struct timespec timeout;
        make_timeout(&timeout, 1);
        pthread_cond_timedwait(&queue->cond, &queue->lock, &timeout);
    }

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    *patient_id_out = queue->patient_ids[queue->head];
    queue->head = (queue->head + 1) % nurse_queue_capacity;
    queue->count--;

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

static void update_metrics_after_discharge(const ActivePatient *active_patient)
{
    time_t discharge_time = time(NULL);
    double waiting_time;
    double turnaround_time;

    if (active_patient == NULL) {
        return;
    }

    waiting_time = difftime(active_patient->start_time, active_patient->arrival_time);
    turnaround_time = difftime(discharge_time, active_patient->arrival_time);

    pthread_mutex_lock(&metrics_lock);
    total_waiting_time += waiting_time;
    total_turnaround_time += turnaround_time;
    completed_patients++;

    if (ward != NULL) {
        ward->total_patients_served++;
    }

    append_schedule_log("discharged patient=%d waiting=%.2f turnaround=%.2f avg_waiting=%.2f avg_turnaround=%.2f",
                        active_patient->patient_id,
                        waiting_time,
                        turnaround_time,
                        total_waiting_time / completed_patients,
                        total_turnaround_time / completed_patients);

    pthread_mutex_unlock(&metrics_lock);
}

static void *nurse_thread(void *arg)
{
    int nurse_index = *(int *)arg;
    const char *nurse_bed_type = nurse_queues[nurse_index].bed_type;

    safe_printf("nurse-%s: started\n", nurse_bed_type);

    while (keep_running) {
        int patient_id;
        ActivePatient active_patient;

        if (pop_nurse_event(nurse_index, &patient_id) == -1) {
            continue;
        }

        safe_printf("nurse-%s: discharge event received for patient %d\n", nurse_bed_type, patient_id);

        if (remove_active_patient(patient_id, &active_patient) == -1) {
            safe_printf("nurse-%s: patient %d not found in active table\n", nurse_bed_type, patient_id);
            continue;
        }

        pthread_mutex_lock(&bed_lock);
        safe_printf("nurse-%s: ward before coalescing\n", nurse_bed_type);
        print_ward_map(stdout, ward);

        if (free_bed(ward, patient_id) == 0) {
            safe_printf("nurse-%s: ward after coalescing\n", nurse_bed_type);
            print_ward_map(stdout, ward);
            log_memory_event("deallocation-coalescing", ward);
            pthread_cond_broadcast(&bed_freed_cond);
        }

        pthread_mutex_unlock(&bed_lock);

        release_capacity_for_bed_type(active_patient.bed_type);
        update_metrics_after_discharge(&active_patient);

        append_mmap_record("discharge patient=%d type=%s partition=%d", patient_id, active_patient.bed_type, active_patient.partition_id);
    }

    safe_printf("nurse-%s: stopped\n", nurse_bed_type);
    return NULL;
}

static void route_discharge_patient(int patient_id)
{
    ActivePatient active_patient;
    int nurse_index;

    if (get_active_patient(patient_id, &active_patient) == -1) {
        append_schedule_log("discharge notification for unknown patient=%d", patient_id);
        return;
    }

    nurse_index = nurse_index_from_bed_type(active_patient.bed_type);

    if (push_nurse_event(nurse_index, patient_id) == -1) {
        append_schedule_log("nurse queue full for patient=%d", patient_id);
    }
}

static void handle_discharge_line(const char *line)
{
    int patient_id;

    if (parse_int_value(line, &patient_id) == -1) {
        append_schedule_log("ignored invalid discharge line: %s", line);
        return;
    }

    safe_printf("discharge-listener: patient %d finished treatment\n", patient_id);
    route_discharge_patient(patient_id);
}

static void *discharge_listener_thread(void *arg)
{
    int fifo_fd;
    char read_buffer[read_buffer_size];
    char line_buffer[line_buffer_size];
    size_t line_length = 0;

    (void)arg;

    fifo_fd = open(discharge_fifo_path, O_RDWR | O_NONBLOCK);
    if (fifo_fd == -1) {
        safe_printf("discharge-listener: could not open discharge fifo: %s\n", strerror(errno));
        return NULL;
    }

    safe_printf("discharge-listener: listening on %s\n", discharge_fifo_path);

    while (keep_running) {
        ssize_t bytes_read = read(fifo_fd, read_buffer, sizeof(read_buffer));

        if (bytes_read > 0) {
            ssize_t i;

            for (i = 0; i < bytes_read; i++) {
                char character = read_buffer[i];

                if (character == '\n') {
                    line_buffer[line_length] = '\0';
                    handle_discharge_line(line_buffer);
                    line_length = 0;
                } else if (line_length < sizeof(line_buffer) - 1) {
                    line_buffer[line_length] = character;
                    line_length++;
                }
            }
        } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            safe_printf("discharge-listener: fifo read error: %s\n", strerror(errno));
            break;
        } else {
            sleep_millis(100);
        }
    }

    close(fifo_fd);
    safe_printf("discharge-listener: stopped\n");
    return NULL;
}

static void stop_active_children(void)
{
    int i;

    pthread_mutex_lock(&active_lock);

    for (i = 0; i < max_active_patients; i++) {
        if (active_patients[i].is_active && active_patients[i].pid > 0) {
            kill(active_patients[i].pid, SIGTERM);
        }
    }

    pthread_mutex_unlock(&active_lock);
}

static void free_remaining_beds(void)
{
    int i;

    if (ward == NULL) {
        return;
    }

    pthread_mutex_lock(&bed_lock);

    for (i = 0; i < ward->partition_count; i++) {
        ward->partitions[i].is_free = 1;
        ward->partitions[i].patient_id = -1;
        snprintf(ward->partitions[i].bed_type, sizeof(ward->partitions[i].bed_type), "%s", bed_type_free);
    }

    while (ward->partition_count > 1) {
        ward->partitions[0].size += ward->partitions[1].size;
        memmove(&ward->partitions[1], &ward->partitions[2], (size_t)(ward->partition_count - 2) * sizeof(BedPartition));
        ward->partition_count--;
    }

    ward->active_patients = 0;
    update_page_table(ward);
    log_memory_event("shutdown-free-all", ward);

    pthread_mutex_unlock(&bed_lock);
}

static void write_pid_file(void)
{
    FILE *file = fopen(pid_file_path, "w");

    if (file == NULL) {
        return;
    }

    fprintf(file, "%ld\n", (long)getpid());
    fclose(file);
}

static void clear_log_files(void)
{
    FILE *file;

    file = fopen(schedule_log_path, "w");
    if (file != NULL) {
        fclose(file);
    }

    file = fopen(memory_log_path, "w");
    if (file != NULL) {
        fclose(file);
    }
}

static void simulate_fcfs(FILE *file, const SimJob *jobs, int job_count)
{
    SimJob local_jobs[8];
    int i;
    int j;
    int time_now = 0;
    double total_wait = 0.0;
    double total_turnaround = 0.0;

    for (i = 0; i < job_count; i++) {
        local_jobs[i] = jobs[i];
    }

    for (i = 0; i < job_count; i++) {
        for (j = i + 1; j < job_count; j++) {
            if (local_jobs[j].arrival < local_jobs[i].arrival) {
                SimJob temp = local_jobs[i];
                local_jobs[i] = local_jobs[j];
                local_jobs[j] = temp;
            }
        }
    }

    fprintf(file, "\nfcfs simulation\n");
    fprintf(file, "gantt: ");

    for (i = 0; i < job_count; i++) {
        int start;
        int finish;
        int wait;
        int turnaround;

        if (time_now < local_jobs[i].arrival) {
            time_now = local_jobs[i].arrival;
        }

        start = time_now;
        finish = start + local_jobs[i].burst;
        wait = start - local_jobs[i].arrival;
        turnaround = finish - local_jobs[i].arrival;
        time_now = finish;

        total_wait += wait;
        total_turnaround += turnaround;

        fprintf(file, "| %s %d-%d ", local_jobs[i].name, start, finish);
    }

    fprintf(file, "|\n");
    fprintf(file, "average waiting time: %.2f\n", total_wait / job_count);
    fprintf(file, "average turnaround time: %.2f\n", total_turnaround / job_count);
}

static void simulate_priority(FILE *file, const SimJob *jobs, int job_count)
{
    SimJob local_jobs[8];
    int completed = 0;
    int time_now = 0;
    double total_wait = 0.0;
    double total_turnaround = 0.0;
    int i;

    for (i = 0; i < job_count; i++) {
        local_jobs[i] = jobs[i];
        local_jobs[i].done = 0;
    }

    fprintf(file, "\npriority scheduling simulation\n");
    fprintf(file, "gantt: ");

    while (completed < job_count) {
        int best_index = -1;
        int start;
        int finish;
        int wait;
        int turnaround;

        for (i = 0; i < job_count; i++) {
            if (local_jobs[i].done || local_jobs[i].arrival > time_now) {
                continue;
            }

            if (best_index == -1 || local_jobs[i].priority < local_jobs[best_index].priority) {
                best_index = i;
            }
        }

        if (best_index == -1) {
            time_now++;
            continue;
        }

        start = time_now;
        finish = start + local_jobs[best_index].burst;
        wait = start - local_jobs[best_index].arrival;
        turnaround = finish - local_jobs[best_index].arrival;
        time_now = finish;
        local_jobs[best_index].done = 1;
        completed++;

        total_wait += wait;
        total_turnaround += turnaround;

        fprintf(file, "| %s(p%d) %d-%d ", local_jobs[best_index].name, local_jobs[best_index].priority, start, finish);
    }

    fprintf(file, "|\n");
    fprintf(file, "average waiting time: %.2f\n", total_wait / job_count);
    fprintf(file, "average turnaround time: %.2f\n", total_turnaround / job_count);
}

static void run_scheduling_simulations(void)
{
    FILE *file;
    SimJob jobs[] = {
        {"p1", 0, 6, 2, 0},
        {"p2", 1, 3, 1, 0},
        {"p3", 2, 4, 4, 0},
        {"p4", 3, 5, 3, 0},
        {"p5", 4, 2, 1, 0}
    };
    int job_count = (int)(sizeof(jobs) / sizeof(jobs[0]));

    file = fopen(schedule_log_path, "a");
    if (file == NULL) {
        return;
    }

    fprintf(file, "hospital scheduling simulations\n");
    fprintf(file, "priority note: lower number means higher emergency priority\n");

    simulate_fcfs(file, jobs, job_count);
    simulate_priority(file, jobs, job_count);

    fclose(file);
}

static int parse_command_line(int argc, char *argv[])
{
    int i;

    current_strategy = allocation_best;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
            current_strategy = parse_allocation_strategy(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: %s [--strategy best|first|worst]\n", argv[0]);
            return 1;
        }
    }

    return 0;
}

static void print_startup_banner(void)
{
    safe_printf("============================================================\n");
    safe_printf("hospital patient triage and bed allocator started\n");
    safe_printf("strategy: %s\n", allocation_strategy_name(current_strategy));
    safe_printf("ward capacity: icu=%d isolation=%d general=%d total_units=%d\n", icu_capacity, isolation_capacity, general_capacity, total_ward_units);
    safe_printf("triage fifo: %s\n", triage_fifo_path);
    safe_printf("discharge fifo: %s\n", discharge_fifo_path);
    safe_printf("============================================================\n");
}

static void print_final_summary(void)
{
    double average_wait = 0.0;
    double average_turnaround = 0.0;

    pthread_mutex_lock(&metrics_lock);
    if (completed_patients > 0) {
        average_wait = total_waiting_time / completed_patients;
        average_turnaround = total_turnaround_time / completed_patients;
    }
    pthread_mutex_unlock(&metrics_lock);

    safe_printf("============================================================\n");
    safe_printf("hospital shutdown summary\n");
    safe_printf("patients completed: %d\n", completed_patients);
    safe_printf("average waiting time: %.2f seconds\n", average_wait);
    safe_printf("average turnaround time: %.2f seconds\n", average_turnaround);

    if (ward != NULL) {
        safe_printf("beds freed: %d\n", ward->beds_freed);
        safe_printf("total patients served in shared memory: %d\n", ward->total_patients_served);
    }

    safe_printf("logs written to %s and %s\n", schedule_log_path, memory_log_path);
    safe_printf("============================================================\n");
}

int main(int argc, char *argv[])
{
    pthread_t receptionist_tid;
    pthread_t scheduler_tid;
    pthread_t discharge_tid;
    pthread_t nurse_tids[3];
    int nurse_indices[3] = {0, 1, 2};
    int i;

    if (parse_command_line(argc, argv) != 0) {
        return 0;
    }

    mkdir(logs_dir, 0777);
    clear_log_files();
    init_mmap_log();

    setup_signal_handlers();

    if (make_fifo_if_needed(triage_fifo_path) == -1 || make_fifo_if_needed(discharge_fifo_path) == -1) {
        cleanup_ipc();
        return 1;
    }

    if (init_shared_ward() == -1) {
        cleanup_ipc();
        return 1;
    }

    if (init_named_semaphores() == -1) {
        cleanup_ipc();
        return 1;
    }

    write_pid_file();
    init_patient_queue();
    init_nurse_queues();
    run_scheduling_simulations();
    print_startup_banner();

    if (pthread_create(&receptionist_tid, NULL, receptionist_thread, NULL) != 0) {
        fprintf(stderr, "could not create receptionist thread\n");
        cleanup_ipc();
        return 1;
    }

    if (pthread_create(&scheduler_tid, NULL, scheduler_thread, NULL) != 0) {
        fprintf(stderr, "could not create scheduler thread\n");
        keep_running = 0;
    }

    if (pthread_create(&discharge_tid, NULL, discharge_listener_thread, NULL) != 0) {
        fprintf(stderr, "could not create discharge listener thread\n");
        keep_running = 0;
    }

    for (i = 0; i < 3; i++) {
        if (pthread_create(&nurse_tids[i], NULL, nurse_thread, &nurse_indices[i]) != 0) {
            fprintf(stderr, "could not create nurse thread %d\n", i);
            keep_running = 0;
        }
    }

    while (keep_running) {
        sleep_millis(500);
    }

    safe_printf("admissions: shutdown signal received\n");
    append_schedule_log("shutdown signal received");

    stop_active_children();

    sem_post(&queue_items);
    pthread_cond_broadcast(&bed_freed_cond);

    for (i = 0; i < 3; i++) {
        pthread_mutex_lock(&nurse_queues[i].lock);
        pthread_cond_broadcast(&nurse_queues[i].cond);
        pthread_mutex_unlock(&nurse_queues[i].lock);
    }

    pthread_join(receptionist_tid, NULL);
    pthread_join(scheduler_tid, NULL);
    pthread_join(discharge_tid, NULL);

    for (i = 0; i < 3; i++) {
        pthread_join(nurse_tids[i], NULL);
    }

    free_remaining_beds();
    print_final_summary();

    destroy_patient_queue();
    destroy_nurse_queues();
    cleanup_ipc();

    return 0;
}
