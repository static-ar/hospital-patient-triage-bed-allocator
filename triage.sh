#!/bin/bash
# ============================================================
# project : hospital patient triage and bed allocator
# script  : stress_test.sh
# group   : group xx
# members : Muhammad Talha (23F-0511), Abdul Rafay (23F-0591),
#           Masooma Mirza (23F-0876)
# purpose : send 20 quick patient arrivals to test concurrency
# usage   : ./scripts/stress_test.sh
# ============================================================

set -u

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
triage_fifo="/tmp/triage_fifo"

cd "$project_root" || exit 1

if [[ ! -p "$triage_fifo" ]]; then
    echo
    echo "error: admissions manager is not running"
    echo "hint : run ./scripts/start_hospital.sh best first"
    echo
    exit 1
fi

names=(ali sara ahmed fatima hassan zain maryam usman noor hamza ayesha bilal hina danish iqra fahad laiba saad rabia talha)

print_line() {
    printf '%*s\n' 86 '' | tr ' ' '-'
}

get_priority() {
    local severity="$1"

    if (( severity >= 9 )); then
        echo 1
    elif (( severity >= 7 )); then
        echo 2
    elif (( severity >= 5 )); then
        echo 3
    elif (( severity >= 3 )); then
        echo 4
    else
        echo 5
    fi
}

echo
echo "stress test started: sending 20 patients"
print_line
printf '%-5s %-14s %-8s %-10s %-10s %-12s %-12s %-12s\n' \
    "no" "name" "age" "severity" "priority" "care_units" "infectious" "status"
print_line

for index in "${!names[@]}"; do
    patient_no=$((index + 1))
    age=$((18 + (index % 50)))
    severity=$((1 + (RANDOM % 10)))
    priority="$(get_priority "$severity")"

    if (( index % 7 == 0 )); then
        infectious_flag=1
        infectious_text="yes"
    else
        infectious_flag=0
        infectious_text="no"
    fi

    if (( priority <= 2 )); then
        care_units=3
    elif (( priority == 3 || infectious_flag == 1 )); then
        care_units=2
    else
        care_units=1
    fi

    patient_record="${names[$index]}|${age}|${severity}|${priority}|${care_units}|${infectious_flag}"
    printf '%s\n' "$patient_record" > "$triage_fifo"

    printf '%-5s %-14s %-8s %-10s %-10s %-12s %-12s %-12s\n' \
        "$patient_no" "${names[$index]}" "$age" "$severity" "$priority" "$care_units" "$infectious_text" "sent"

    sleep 0.08
done

print_line
echo "stress test completed: 20 patients sent"
echo "watch live log      : tail -f logs/hospital_runtime.log"
echo "view schedule log   : cat logs/schedule_log.txt"
echo "view memory log     : cat logs/memory_log.txt"
echo
