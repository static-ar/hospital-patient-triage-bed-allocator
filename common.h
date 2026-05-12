#ifndef bed_allocator_h
#define bed_allocator_h

#include <stdio.h>

#include "common.h"

void init_ward_state(SharedWardState *ward);
AllocationStrategy parse_allocation_strategy(const char *value);
const char *allocation_strategy_name(AllocationStrategy strategy);
const char *bed_type_for_patient(const PatientRecord *patient);

int allocate_bed(SharedWardState *ward, const PatientRecord *patient, AllocationStrategy strategy, int *partition_id_out);
int free_bed(SharedWardState *ward, int patient_id);

void update_page_table(SharedWardState *ward);
void print_ward_map(FILE *stream, const SharedWardState *ward);
void log_memory_event(const char *event_name, const SharedWardState *ward);

int total_free_units(const SharedWardState *ward);
int largest_free_block(const SharedWardState *ward);
double external_fragmentation_percent(const SharedWardState *ward);
int internal_fragmentation_units(int care_units);

#endif
