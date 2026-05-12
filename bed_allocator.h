#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================
 * project : hospital patient triage and bed allocator
 * file    : bed_allocator.c
 * group   : group xx
 * members : Muhammad Talha (23F-0511), Abdul Rafay (23F-0591),
 *           Masooma Mirza (23F-0876)
 * purpose : first-fit, best-fit, worst-fit, coalescing,
 *           fragmentation, and paging simulation
 * ============================================================
 */

#include "bed_allocator.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void set_partition(BedPartition *partition, int partition_id, int start_unit, int size, int is_free, int patient_id, const char *bed_type)
{
    partition->partition_id = partition_id;
    partition->start_unit = start_unit;
    partition->size = size;
    partition->is_free = is_free;
    partition->patient_id = patient_id;
    snprintf(partition->bed_type, sizeof(partition->bed_type), "%s", bed_type);
}

static void remove_partition_at(SharedWardState *ward, int index)
{
    int i;

    if (index < 0 || index >= ward->partition_count) {
        return;
    }

    for (i = index; i < ward->partition_count - 1; i++) {
        ward->partitions[i] = ward->partitions[i + 1];
    }

    ward->partition_count--;
}

void init_ward_state(SharedWardState *ward)
{
    int start_unit = 0;
    int partition_id = 1;
    int i;

    if (ward == NULL) {
        return;
    }

    memset(ward, 0, sizeof(*ward));
    ward->is_initialized = 1;
    ward->partition_count = 0;
    ward->next_partition_id = 1;

    for (i = 0; i < icu_capacity; i++) {
        set_partition(&ward->partitions[ward->partition_count], partition_id, start_unit, icu_units, 1, -1, bed_type_free);
        ward->partition_count++;
        partition_id++;
        start_unit += icu_units;
    }

    for (i = 0; i < isolation_capacity; i++) {
        set_partition(&ward->partitions[ward->partition_count], partition_id, start_unit, isolation_units, 1, -1, bed_type_free);
        ward->partition_count++;
        partition_id++;
        start_unit += isolation_units;
    }

    for (i = 0; i < general_capacity; i++) {
        set_partition(&ward->partitions[ward->partition_count], partition_id, start_unit, general_units, 1, -1, bed_type_free);
        ward->partition_count++;
        partition_id++;
        start_unit += general_units;
    }

    ward->next_partition_id = partition_id;
    update_page_table(ward);
}

AllocationStrategy parse_allocation_strategy(const char *value)
{
    if (value == NULL) {
        return allocation_best;
    }

    if (strcmp(value, "first") == 0) {
        return allocation_first;
    }

    if (strcmp(value, "worst") == 0) {
        return allocation_worst;
    }

    return allocation_best;
}

const char *allocation_strategy_name(AllocationStrategy strategy)
{
    if (strategy == allocation_first) {
        return "first-fit";
    }

    if (strategy == allocation_worst) {
        return "worst-fit";
    }

    return "best-fit";
}

const char *bed_type_for_patient(const PatientRecord *patient)
{
    if (patient == NULL) {
        return bed_type_general;
    }

    if (patient->care_units >= icu_units || patient->priority <= 2) {
        return bed_type_icu;
    }

    if (patient->care_units >= isolation_units || patient->priority == 3) {
        return bed_type_isolation;
    }

    return bed_type_general;
}

int allocate_bed(SharedWardState *ward, const PatientRecord *patient, AllocationStrategy strategy, int *partition_id_out)
{
    int candidate_index = -1;
    int candidate_size = 0;
    int i;
    int required_units;
    BedPartition old_partition;
    BedPartition *selected_partition;
    BedPartition *remainder_partition;

    if (ward == NULL || patient == NULL || partition_id_out == NULL) {
        return -1;
    }

    required_units = patient->care_units;

    if (required_units <= 0) {
        required_units = general_units;
    }

    if (ward->partition_count >= max_partitions - 1) {
        return -1;
    }

    for (i = 0; i < ward->partition_count; i++) {
        BedPartition *partition = &ward->partitions[i];

        if (!partition->is_free || partition->size < required_units) {
            continue;
        }

        if (strategy == allocation_first) {
            candidate_index = i;
            break;
        }

        if (candidate_index == -1) {
            candidate_index = i;
            candidate_size = partition->size;
            continue;
        }

        if (strategy == allocation_best && partition->size < candidate_size) {
            candidate_index = i;
            candidate_size = partition->size;
        }

        if (strategy == allocation_worst && partition->size > candidate_size) {
            candidate_index = i;
            candidate_size = partition->size;
        }
    }

    if (candidate_index == -1) {
        return -1;
    }

    old_partition = ward->partitions[candidate_index];
    selected_partition = &ward->partitions[candidate_index];

    if (old_partition.size > required_units) {
        for (i = ward->partition_count; i > candidate_index + 1; i--) {
            ward->partitions[i] = ward->partitions[i - 1];
        }

        ward->partition_count++;
        remainder_partition = &ward->partitions[candidate_index + 1];
        set_partition(remainder_partition, ward->next_partition_id, old_partition.start_unit + required_units, old_partition.size - required_units, 1, -1, bed_type_free);
        ward->next_partition_id++;
    }

    selected_partition = &ward->partitions[candidate_index];
    selected_partition->size = required_units;
    selected_partition->is_free = 0;
    selected_partition->patient_id = patient->patient_id;
    snprintf(selected_partition->bed_type, sizeof(selected_partition->bed_type), "%s", bed_type_for_patient(patient));

    *partition_id_out = selected_partition->partition_id;
    ward->active_patients++;
    update_page_table(ward);

    return 0;
}

int free_bed(SharedWardState *ward, int patient_id)
{
    int index = -1;
    int i;

    if (ward == NULL) {
        return -1;
    }

    for (i = 0; i < ward->partition_count; i++) {
        if (!ward->partitions[i].is_free && ward->partitions[i].patient_id == patient_id) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return -1;
    }

    ward->partitions[index].is_free = 1;
    ward->partitions[index].patient_id = -1;
    snprintf(ward->partitions[index].bed_type, sizeof(ward->partitions[index].bed_type), "%s", bed_type_free);

    if (ward->active_patients > 0) {
        ward->active_patients--;
    }

    ward->beds_freed++;

    if (index > 0 && ward->partitions[index - 1].is_free) {
        ward->partitions[index - 1].size += ward->partitions[index].size;
        remove_partition_at(ward, index);
        index--;
    }

    if (index < ward->partition_count - 1 && ward->partitions[index + 1].is_free) {
        ward->partitions[index].size += ward->partitions[index + 1].size;
        remove_partition_at(ward, index + 1);
    }

    update_page_table(ward);
    return 0;
}

void update_page_table(SharedWardState *ward)
{
    int i;

    if (ward == NULL) {
        return;
    }

    for (i = 0; i < max_pages; i++) {
        ward->page_table[i] = -1;
    }

    for (i = 0; i < ward->partition_count; i++) {
        const BedPartition *partition = &ward->partitions[i];
        int unit;

        if (partition->is_free) {
            continue;
        }

        for (unit = partition->start_unit; unit < partition->start_unit + partition->size && unit < total_ward_units; unit++) {
            int page_index = unit / page_size_units;

            if (page_index >= 0 && page_index < max_pages) {
                if (ward->page_table[page_index] == -1 || ward->page_table[page_index] == partition->patient_id) {
                    ward->page_table[page_index] = partition->patient_id;
                } else {
                    ward->page_table[page_index] = -2;
                }
            }
        }
    }
}

int total_free_units(const SharedWardState *ward)
{
    int total = 0;
    int i;

    if (ward == NULL) {
        return 0;
    }

    for (i = 0; i < ward->partition_count; i++) {
        if (ward->partitions[i].is_free) {
            total += ward->partitions[i].size;
        }
    }

    return total;
}

int largest_free_block(const SharedWardState *ward)
{
    int largest = 0;
    int i;

    if (ward == NULL) {
        return 0;
    }

    for (i = 0; i < ward->partition_count; i++) {
        if (ward->partitions[i].is_free && ward->partitions[i].size > largest) {
            largest = ward->partitions[i].size;
        }
    }

    return largest;
}

double external_fragmentation_percent(const SharedWardState *ward)
{
    int total_free = total_free_units(ward);
    int largest = largest_free_block(ward);

    if (total_free <= 0) {
        return 0.0;
    }

    return (1.0 - ((double)largest / (double)total_free)) * 100.0;
}

int internal_fragmentation_units(int care_units)
{
    int remainder;

    if (care_units <= 0) {
        return 0;
    }

    remainder = care_units % page_size_units;

    if (remainder == 0) {
        return 0;
    }

    return page_size_units - remainder;
}

void print_ward_map(FILE *stream, const SharedWardState *ward)
{
    char unit_map[total_ward_units + 1];
    int i;

    if (stream == NULL || ward == NULL) {
        return;
    }

    for (i = 0; i < total_ward_units; i++) {
        unit_map[i] = '.';
    }
    unit_map[total_ward_units] = '\0';

    for (i = 0; i < ward->partition_count; i++) {
        const BedPartition *partition = &ward->partitions[i];
        int unit;
        char symbol = '.';

        if (!partition->is_free) {
            if (strcmp(partition->bed_type, bed_type_icu) == 0) {
                symbol = 'i';
            } else if (strcmp(partition->bed_type, bed_type_isolation) == 0) {
                symbol = 's';
            } else {
                symbol = 'g';
            }
        }

        for (unit = partition->start_unit; unit < partition->start_unit + partition->size && unit < total_ward_units; unit++) {
            unit_map[unit] = symbol;
        }
    }

    fprintf(stream, "ward map: %s\n", unit_map);
    fprintf(stream, "partitions:\n");

    for (i = 0; i < ward->partition_count; i++) {
        const BedPartition *partition = &ward->partitions[i];

        fprintf(stream,
                "  id=%02d start=%02d size=%02d status=%s patient=%d type=%s\n",
                partition->partition_id,
                partition->start_unit,
                partition->size,
                partition->is_free ? "free" : "occupied",
                partition->patient_id,
                partition->bed_type);
    }

    fprintf(stream, "page table: ");
    for (i = 0; i < max_pages; i++) {
        if (ward->page_table[i] == -1) {
            fprintf(stream, "[p%d:free]", i);
        } else if (ward->page_table[i] == -2) {
            fprintf(stream, "[p%d:mixed]", i);
        } else {
            fprintf(stream, "[p%d:%d]", i, ward->page_table[i]);
        }
    }
    fprintf(stream, "\n");
}

void log_memory_event(const char *event_name, const SharedWardState *ward)
{
    FILE *file;
    time_t now;
    struct tm *local_time;
    char time_text[64];

    if (ward == NULL) {
        return;
    }

    file = fopen(memory_log_path, "a");
    if (file == NULL) {
        return;
    }

    now = time(NULL);
    local_time = localtime(&now);

    if (local_time != NULL) {
        strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", local_time);
    } else {
        snprintf(time_text, sizeof(time_text), "unknown-time");
    }

    fprintf(file,
            "[%s] event=%s total_free=%d largest_free=%d external_fragmentation=%.2f%% active_patients=%d\n",
            time_text,
            event_name != NULL ? event_name : "unknown",
            total_free_units(ward),
            largest_free_block(ward),
            external_fragmentation_percent(ward),
            ward->active_patients);

    fclose(file);
}
