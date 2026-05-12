#!/bin/bash
# ============================================================
# project : hospital patient triage and bed allocator
# script  : start_hospital.sh
# members : Muhammad Talha (23F-0511), Abdul Rafay (23F-0591),
#           Masooma Mirza (23F-0876)
# purpose : build and start the admissions manager in background
# usage   : ./scripts/start_hospital.sh [best|first|worst]
# ============================================================

set -u

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
strategy="${1:-best}"
pid_file="/tmp/hospital_admissions.pid"
build_log="/tmp/hospital_build.log"

print_line() {
    printf '%*s\n' 72 '' | tr ' ' '='
}

print_row() {
    printf '| %-24s | %-39s |\n' "$1" "$2"
}

case "$strategy" in
    best|first|worst)
        ;;
    *)
        echo
        echo "error: strategy must be best, first, or worst"
        echo "usage: ./scripts/start_hospital.sh [best|first|worst]"
        exit 1
        ;;
esac

cd "$project_root" || exit 1

if [[ -f "$pid_file" ]]; then
    old_pid="$(cat "$pid_file" 2>/dev/null || true)"

    if [[ -n "$old_pid" ]] && kill -0 "$old_pid" 2>/dev/null; then
        echo
        print_line
        print_row "status" "hospital already running"
        print_row "pid" "$old_pid"
        print_row "stop command" "./scripts/stop_hospital.sh"
        print_line
        echo
        exit 0
    fi
fi

rm -f /tmp/triage_fifo /tmp/discharge_fifo /tmp/hospital_admissions.pid
rm -f /dev/shm/hospital_bed_shm
rm -f /dev/shm/sem.sem_icu_limit /dev/shm/sem.sem_isolation_limit 2>/dev/null || true

mkdir -p logs

echo
print_line
print_row "project" "hospital patient triage and bed allocator"
print_row "build" "compiling project files"
print_line

if ! make all > "$build_log" 2>&1; then
    echo
    echo "build failed. details:"
    cat "$build_log"
    exit 1
fi

print_row "build" "successful"

./build/admissions --strategy "$strategy" > logs/hospital_runtime.log 2>&1 &
admissions_pid=$!

echo "$admissions_pid" > "$pid_file"

sleep 1

if ! kill -0 "$admissions_pid" 2>/dev/null; then
    echo
    echo "error: admissions failed to start"
    echo "check: logs/hospital_runtime.log"
    exit 1
fi

print_row "status" "running"
print_row "process id" "$admissions_pid"
print_row "strategy" "$strategy"
print_row "icu beds" "4"
print_row "isolation beds" "4"
print_row "general beds" "12"
print_row "total care units" "32"
print_line

echo
echo "quick commands:"
echo
printf '  %-18s %s\n' "send patient" "./scripts/triage.sh ali 20 8"
printf '  %-18s %s\n' "infectious case" "./scripts/triage.sh sara 22 5 yes"
printf '  %-18s %s\n' "stress test" "./scripts/stress_test.sh"
printf '  %-18s %s\n' "live log" "tail -f logs/hospital_runtime.log"
printf '  %-18s %s\n' "stop system" "./scripts/stop_hospital.sh"
echo
print_line
echo
