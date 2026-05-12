# linkedin post draft

completed my operating systems lab semester project: hospital patient triage and bed allocator.

in this project, we simulated an emergency room using linux system programming in c. patients arrive through a triage shell script, receive a priority based on severity, and are allocated to icu, isolation, or general care using memory allocation strategies.

os concepts implemented:

- fork and execv for patient processes
- posix threads for receptionist, scheduler, and nurses
- fifos and shared memory for ipc
- mutexes, condition variables, and semaphores
- priority scheduling with gantt-style logs
- best-fit, first-fit, worst-fit allocation
- coalescing and fragmentation reporting
- paging simulation

one challenge was debugging synchronization between the scheduler thread and nurse threads. both needed access to the shared ward map, so we used a mutex around bed allocation and deallocation, plus a condition variable to wake the scheduler when a bed became free.

#operatingsystems #linux #cprogramming #posix #systemsprogramming


group members: Muhammad Talha (23F-0511), Abdul Rafay (23F-0591), Masooma Mirza (23F-0876)
