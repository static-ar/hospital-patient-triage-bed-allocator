# Hospital Patient Triage & Bed Allocator

## Operating Systems Lab Semester Project


---

# Project Overview

This project implements a Linux-based hospital emergency room simulator using C and POSIX APIs on Ubuntu Linux inside VMware.

The system simulates patient triage, emergency priority scheduling, ICU/Isolation/General bed allocation, concurrent patient handling, memory allocation strategies, fragmentation reporting, and paging simulation.

The project demonstrates multiple core Operating Systems concepts in one integrated application.

---

# Implemented Operating Systems Concepts

## Process Management

- `fork()`
- `execv()`
- `waitpid(WNOHANG)`
- `SIGCHLD` handling
- Concurrent patient processes

## Inter-Process Communication (IPC)

- Named FIFOs
- Shared Memory
- Patient admission/discharge communication

## CPU Scheduling

- FCFS Scheduling
- Priority Scheduling
- Gantt chart logs
- Average waiting time
- Average turnaround time

## Threads & Synchronization

- POSIX Threads
- Receptionist thread
- Scheduler thread
- Nurse threads
- Mutex locks
- Condition variables
- Producer-consumer synchronization

## Semaphores

- ICU capacity semaphore
- Isolation capacity semaphore
- Bounded queue synchronization

## Memory Management

- Best-Fit allocation
- First-Fit allocation
- Worst-Fit allocation
- Coalescing
- External fragmentation
- Paging simulation
- Internal fragmentation reporting

---

# Project Structure

```text
OS_Project/
├── src/
│   ├── admissions.c
│   ├── patient_simulator.c
│   ├── bed_allocator.c
│   ├── bed_allocator.h
│   └── common.h
│
├── scripts/
│   ├── start_hospital.sh
│   ├── stop_hospital.sh
│   ├── triage.sh
│   ├── stress_test.sh
│   └── view_logs.sh
│
├── logs/
│   ├── hospital_runtime.log
│   ├── schedule_log.txt
│   ├── memory_log.txt
│   └── patient_records.dat
│
├── docs/
├── Makefile
├── README.md
├── report.pdf
├── self_eval.pdf
└── demo_video.mp4
```

---

# Requirements

Ubuntu Linux inside VMware is recommended.

Install dependencies:

```bash
sudo apt update
sudo apt install build-essential gcc make valgrind unzip zip -y
```

---

# Compilation

```bash
make clean
make all
```

---

# Start Hospital System

Best-Fit strategy:

```bash
./scripts/start_hospital.sh best
```

First-Fit strategy:

```bash
./scripts/start_hospital.sh first
```

Worst-Fit strategy:

```bash
./scripts/start_hospital.sh worst
```

---

# Send Patient

Normal patient:

```bash
./scripts/triage.sh ali 20 8
```

Infectious patient:

```bash
./scripts/triage.sh sara 22 5 yes
```

Command format:

```bash
./scripts/triage.sh <name> <age> <severity 1-10> [infectious yes|no]
```

---

# Stress Test

The stress test sends 20 rapid patient arrivals to test concurrency.

```bash
./scripts/stress_test.sh
```

---

# View Logs

Schedule log:

```bash
./scripts/view_logs.sh schedule
```

Memory log:

```bash
./scripts/view_logs.sh memory
```

Patient records:

```bash
./scripts/view_logs.sh records
```

Runtime log:

```bash
./scripts/view_logs.sh runtime
```

All logs:

```bash
./scripts/view_logs.sh all
```

---

# Stop Hospital System

```bash
./scripts/stop_hospital.sh
```

---

# Valgrind Testing

```bash
valgrind --leak-check=full --show-leak-kinds=all ./build/admissions --strategy best
```

---

# Example Workflow

```bash
make clean
make all

./scripts/start_hospital.sh best

./scripts/triage.sh talha 21 9
./scripts/triage.sh rafay 22 6
./scripts/triage.sh masooma 21 4 yes

./scripts/stress_test.sh

./scripts/view_logs.sh schedule
./scripts/view_logs.sh memory
./scripts/view_logs.sh records

./scripts/stop_hospital.sh
```

---

# Key Features

- Linux-based hospital emergency room simulation
- Multi-process patient management
- Concurrent patient scheduling
- Shared memory ward state
- Synchronization using mutexes and semaphores
- Memory allocation strategy comparison
- Fragmentation and paging analysis
- Clean shutdown and IPC cleanup
- Organized logging system

---

# Developed Using

- C Language
- POSIX APIs
- GCC Compiler
- Ubuntu Linux
- VMware Workstation

---

# Authors

Muhammad Talha Jawad — 23F-0511  
Abdul Rafay — 23F-0591  
Masooma Mirza — 23F-0876

---

# Course

CL2006 — Operating Systems Lab

FAST National University of Computer and Emerging Sciences
