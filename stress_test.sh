#!/bin/bash
# ============================================================
# project : hospital patient triage and bed allocator
# script  : stop_hospital.sh
# group   : group xx
# members : Muhammad Talha (23F-0511), Abdul Rafay (23F-0591),
#           Masooma Mirza (23F-0876)
# purpose : stop admissions manager and clean ipc resources
# usage   : ./scripts/stop_hospital.sh
# ============================================================

set -u

pid_file="/tmp/hospital_admissions.pid"
runtime_log="logs/hospital_runtime.log"

print_line() {
    printf '%*s\n' 72 '' | tr ' ' '='
}

print_row() {
    printf '| %-28s | %-35s |\n' "$1" "$2"
}

get_last_value() {
    local label="$1"

    if [[ -f "$runtime_log" ]]; then
        grep "$label" "$runtime_log" | tail -n 1 | cut -d ':' -f 2- | sed 's/^ *//'
    fi
}

if [[ -f "$pid_file" ]]; then
    admissions_pid="$(cat "$pid_file" 2>/dev/null || true)"
else
    admissions_pid=""
fi

echo
print_line
print_row "action" "hospital shutdown started"
print_line

if [[ -n "$admissions_pid" ]] && kill -0 "$admissions_pid" 2>/dev/null; then
    print_row "admissions pid" "$admissions_pid"
    print_row "signal" "sending sigterm"

    kill -TERM "$admissions_pid" 2>/dev/null || true

    for _ in {1..12}; do
        if ! kill -0 "$admissions_pid" 2>/dev/null; then
            break
        fi

        sleep 1
    done

    if kill -0 "$admissions_pid" 2>/dev/null; then
        print_row "status" "force stop required"
        kill -KILL "$admissions_pid" 2>/dev/null || true
    else
        print_row "status" "stopped cleanly"
    fi
else
    print_row "status" "admissions manager was not running"
fi

rm -f /tmp/triage_fifo /tmp/discharge_fifo /tmp/hospital_admissions.pid
rm -f /dev/shm/hospital_bed_shm
rm -f /dev/shm/sem.sem_icu_limit /dev/shm/sem.sem_isolation_limit 2>/dev/null || true

print_row "ipc cleanup" "done"
print_line

if [[ -f "$runtime_log" ]]; then
    patients_completed="$(get_last_value "patients completed")"
    average_waiting="$(get_last_value "average waiting time")"
    average_turnaround="$(get_last_value "average turnaround time")"
    beds_freed="$(get_last_value "beds freed")"
    shared_memory_served="$(get_last_value "total patients served in shared memory")"

    echo
    print_line
    print_row "final summary" "hospital patient triage system"
    print_line
    print_row "patients completed" "${patients_completed:-not found}"
    print_row "average waiting time" "${average_waiting:-not found}"
    print_row "average turnaround time" "${average_turnaround:-not found}"
    print_row "beds freed" "${beds_freed:-not found}"
    print_row "shared memory served" "${shared_memory_served:-not found}"
    print_row "schedule log" "logs/schedule_log.txt"
    print_row "memory log" "logs/memory_log.txt"
    print_row "runtime log" "logs/hospital_runtime.log"
    print_line
    echo
else
    echo
    echo "runtime log not found"
    echo
fi
