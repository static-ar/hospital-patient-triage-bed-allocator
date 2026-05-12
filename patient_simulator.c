#ifndef common_h
#define common_h

#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <sys/types.h>

#define project_name "hospital patient triage and bed allocator"

#define triage_fifo_path "/tmp/triage_fifo"
#define discharge_fifo_path "/tmp/discharge_fifo"
#define pid_file_path "/tmp/hospital_admissions.pid"

#define shm_name "/hospital_bed_shm"
#define sem_icu_limit_name "/sem_icu_limit"
#define sem_isolation_limit_name "/sem_isolation_limit"

#define max_name_len 64
#define max_bed_type_len 16
#define max_partitions 128
#define max_active_patients 128
#define patient_queue_capacity 64
#define nurse_queue_capacity 64

#define icu_capacity 4
#define isolation_capacity 4
#define general_capacity 12

#define icu_units 3
#define isolation_units 2
#define general_units 1
#define total_ward_units ((icu_capacity * icu_units) + (isolation_capacity * isolation_units) + (general_capacity * general_units))

#define page_size_units 2
#define max_pages ((total_ward_units + page_size_units - 1) / page_size_units)

#define logs_dir "logs"
#define schedule_log_path "logs/schedule_log.txt"
#define memory_log_path "logs/memory_log.txt"
#define patient_records_path "logs/patient_records.dat"

#define bed_type_icu "ICU"
#define bed_type_isolation "ISOLATION"
#define bed_type_general "GENERAL"
#define bed_type_free "FREE"

/* patient record passed via ipc */
typedef struct {
    int patient_id;
    char name[max_name_len];
    int age;
    int severity;
    int priority;
    int care_units;
    time_t arrival_time;
} PatientRecord;

/* single bed partition in the ward memory model */
typedef struct {
    int partition_id;
    int start_unit;
    int size;
    int is_free;
    int patient_id;
    char bed_type[max_bed_type_len];
} BedPartition;

typedef enum {
    allocation_best = 0,
    allocation_first = 1,
    allocation_worst = 2
} AllocationStrategy;

typedef struct {
    int is_initialized;
    int next_partition_id;
    int partition_count;
    int total_patients_served;
    int beds_freed;
    int active_patients;
    BedPartition partitions[max_partitions];
    int page_table[max_pages];
} SharedWardState;

typedef struct {
    int patient_id;
    pid_t pid;
    int partition_id;
    int priority;
    int care_units;
    time_t arrival_time;
    time_t start_time;
    char bed_type[max_bed_type_len];
    int is_active;
} ActivePatient;

#endif
