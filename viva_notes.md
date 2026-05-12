# hospital patient triage and bed allocator - final report

## 1. title page

project name: hospital patient triage and bed allocator  
course: cl2006 operating systems lab  
group: group xx  
members: Muhammad Talha (23F-0511), Abdul Rafay (23F-0591), Masooma Mirza (23F-0876)  
date: 2026

## 2. introduction

this project implements a linux-based hospital emergency room simulator. patients arrive through a shell triage script, receive a triage priority, and are admitted to icu, isolation, or general care based on severity and care-unit requirement. the system demonstrates process management, inter-process communication, cpu scheduling, synchronization, semaphores, and memory management.

## 3. objectives

- demonstrate linux shell scripting and makefile automation
- create patient processes using `fork()` and `execv()`
- communicate between processes using fifos and shared memory
- schedule patients by emergency priority
- protect shared ward data with mutexes and condition variables
- enforce icu and isolation limits with semaphores
- allocate and free ward memory using best-fit, first-fit, and worst-fit
- report paging and fragmentation statistics

## 4. system design

### architecture

```text
triage.sh
   |
   v
/tmp/triage_fifo
   |
   v
receptionist thread --> bounded priority queue --> scheduler thread
                                               |
                                               v
                                      shared ward memory
                                               |
                                               v
                                     fork + execv patient
                                               |
                                               v
                                   /tmp/discharge_fifo
                                               |
                                               v
discharge listener --> nurse queue --> nurse threads --> free + coalesce bed
```

### main components

- admissions manager: central process that owns shared memory, threads, semaphores, scheduling, and bed allocation.
- patient simulator: child process created through fork and execv. it simulates treatment and sends discharge notification.
- triage script: validates patient input and computes priority.
- bed allocator: implements memory allocation algorithms and fragmentation reports.

## 5. phase 1 documentation

### scripts

`triage.sh` accepts name, age, severity, and optional infectious flag. it validates inputs and writes a formatted patient record to `/tmp/triage_fifo`.

`start_hospital.sh` builds the project, removes stale ipc resources, starts admissions in the background, and prints capacity.

`stop_hospital.sh` sends sigterm, waits for shutdown, removes fifo/shared memory/semaphore files, and prints final log lines.

`stress_test.sh` sends 20 patient arrivals quickly.

### makefile

make targets:

- `make all`: compile admissions and patient simulator
- `make clean`: remove build files and stale ipc resources
- `make run`: start hospital using best-fit
- `make test`: start hospital and run stress test

## 6. phase 2 documentation

### process management

when the scheduler admits a patient, it calls `fork()`. the child process then calls `execv()` and becomes `build/patient_simulator`. the parent stores the child pid in the active patient table. a `sigchld` handler calls `waitpid(-1, null, wnohang)` to remove zombie processes.

### ipc

- triage arrivals use `/tmp/triage_fifo`
- discharge notifications use `/tmp/discharge_fifo`
- ward bitmap and partitions are stored in posix shared memory named `/hospital_bed_shm`
- icu and isolation semaphores are named `/sem_icu_limit` and `/sem_isolation_limit`

### scheduling

patients are stored in a bounded priority queue. lower priority number means more serious patient. priority 1 is admitted before priority 2, and so on.

`logs/schedule_log.txt` includes fcfs and priority scheduling gantt-style simulations with average waiting time and average turnaround time.

## 7. phase 3 documentation

### thread roles

- receptionist thread reads triage fifo and produces patient records
- scheduler thread consumes patient records, allocates beds, and starts patient processes
- nurse threads handle icu, isolation, and general discharges
- discharge listener thread reads discharge fifo and routes events to nurse queues

### synchronization

- `bed_lock` protects shared ward state
- `bed_freed_cond` wakes the scheduler when nurse threads free a bed
- queue semaphores implement bounded producer-consumer behavior
- named semaphores limit icu and isolation admissions

### semaphore blocking scenario

when more than 4 icu-priority patients arrive, the scheduler waits on `/sem_icu_limit`. when an icu patient is discharged, the icu nurse releases the semaphore, allowing the waiting patient to proceed.

## 8. phase 4 documentation

### memory model

ward capacity is 32 care units:

- 4 icu beds x 3 units = 12 units
- 4 isolation beds x 2 units = 8 units
- 12 general beds x 1 unit = 12 units

### allocation strategies

runtime flag:

```bash
./build/admissions --strategy best
./build/admissions --strategy first
./build/admissions --strategy worst
```

best-fit chooses the smallest free partition that can satisfy the required care units. first-fit chooses the first suitable free partition. worst-fit chooses the largest suitable free partition.

### coalescing

when a nurse frees a bed, the allocator checks left and right neighboring partitions. if they are free, it merges them into one larger free block.

### fragmentation

external fragmentation formula:

```text
(1 - largest_free_block / total_free_units) * 100
```

statistics are logged to `logs/memory_log.txt` after every allocation and deallocation.

### paging

page size is 2 care units. the page table records which patient occupies each page. if a patient's care units are not a multiple of page size, internal fragmentation is reported.

## 9. testing

recommended tests:

| test | command | expected result |
|---|---|---|
| build test | `make all` | zero compiler warnings |
| single patient | `./scripts/triage.sh ali 20 8` | patient admitted and discharged |
| invalid input | `./scripts/triage.sh ali x 8` | validation error |
| stress test | `./scripts/stress_test.sh` | 20 patients accepted, no crash |
| strategy test | `./scripts/start_hospital.sh worst` | worst-fit used |
| shutdown test | `./scripts/stop_hospital.sh` | ipc resources removed |

## 10. valgrind output

add screenshot here after running valgrind in vmware ubuntu.

## 11. challenges and lessons learned

- synchronizing shared ward state was important because the scheduler and nurse threads access the same data.
- semaphores prevented more than the allowed number of icu and isolation patients.
- coalescing reduced external fragmentation after discharges.
- fork and execv required careful argument passing and child process cleanup.

## 12. contribution table

| phase | Muhammad Talha (23F-0511) | Abdul Rafay (23F-0591) | Masooma Mirza (23F-0876) |
|---|---|---|---|
| shell scripts | reviewed run flow | triage, start, stop, stress scripts | tested script cases |
| makefile | checked build targets | makefile all, clean, run, test | tested clean/build cycle |
| process management | scheduling-side process start flow | fork, execv, sigchld, waitpid, patient lifecycle | tested concurrent process cases |
| ipc | used ipc in scheduler flow | triage fifo, discharge fifo, shared memory setup/cleanup | verified shared ward state during memory tests |
| scheduling | priority queue, fcfs, priority scheduling, schedule log | helped with patient input test data | verified output metrics |
| threads | receptionist, scheduler, nurse, discharge listener threads | supported integration testing | verified nurse discharge behavior |
| mutex and condition variables | bed lock, queue lock, bed_freed_cond | helped debug shutdown cases | verified no allocation/free conflict in stress run |
| semaphores | icu/isolation semaphores, bounded producer-consumer queue | tested blocking/release scenario | checked semaphore effect on ward state |
| memory allocator | integrated allocator with scheduler | tested patient admission path | best-fit, first-fit, worst-fit, coalescing |
| fragmentation and paging | logged paging output during admissions | tested logs after discharge | external fragmentation, internal fragmentation, page table |
| report and video | report finalization, contribution table, demo video script and voice-over | screenshots and run commands support | memory section and testing notes |

## 13. conclusion

this project combines multiple operating systems concepts in one complete linux application. it demonstrates process creation, inter-process communication, thread synchronization, scheduling, memory allocation, fragmentation, and paging using a hospital triage scenario.
