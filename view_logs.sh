#!/bin/bash
# ============================================================
# project : hospital patient triage and bed allocator
# script  : triage.sh
# group   : group xx
# members : Muhammad Talha (23F-0511), Abdul Rafay (23F-0591),
#           Masooma Mirza (23F-0876)
# purpose : validate patient input, compute priority, and send
#           the patient record to the admissions manager fifo
# usage   : ./scripts/triage.sh <name> <age> <severity 1-10> [infectious yes|no]
# ============================================================

set -u

triage_fifo="/tmp/triage_fifo"

show_usage() {
    echo
    echo "usage:"
    echo "  ./scripts/triage.sh <name> <age> <severity 1-10> [infectious yes|no]"
    echo
    echo "examples:"
    echo "  ./scripts/triage.sh ali 20 8"
    echo "  ./scripts/triage.sh sara 22 5 yes"
    echo
}

is_number() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

print_line() {
    printf '%*s\n' 78 '' | tr ' ' '-'
}

if [[ $# -lt 3 || $# -gt 4 ]]; then
    show_usage
    exit 1
fi

patient_name="$1"
patient_age="$2"
severity="$3"
infectious_input="${4:-no}"

if [[ -z "$patient_name" ]]; then
    echo "error: patient name cannot be empty"
    exit 1
fi

if ! is_number "$patient_age"; then
    echo "error: age must be numeric"
    exit 1
fi

if ! is_number "$severity"; then
    echo "error: severity must be numeric"
    exit 1
fi

if (( patient_age <= 0 || patient_age > 120 )); then
    echo "error: age must be between 1 and 120"
    exit 1
fi

if (( severity < 1 || severity > 10 )); then
    echo "error: severity must be between 1 and 10"
    exit 1
fi

infectious_flag=0

case "${infectious_input,,}" in
    yes|y|true|1)
        infectious_flag=1
        infectious_text="yes"
        ;;
    no|n|false|0)
        infectious_flag=0
        infectious_text="no"
        ;;
    *)
        echo "error: infectious must be yes or no"
        exit 1
        ;;
esac

if (( severity >= 9 )); then
    priority=1
elif (( severity >= 7 )); then
    priority=2
elif (( severity >= 5 )); then
    priority=3
elif (( severity >= 3 )); then
    priority=4
else
    priority=5
fi

if (( priority <= 2 )); then
    care_units=3
    expected_bed="icu"
elif (( priority == 3 || infectious_flag == 1 )); then
    care_units=2
    expected_bed="isolation"
else
    care_units=1
    expected_bed="general"
fi

if [[ ! -p "$triage_fifo" ]]; then
    echo
    echo "error: admissions manager is not running"
    echo "hint : run ./scripts/start_hospital.sh best first"
    echo
    exit 1
fi

patient_record="${patient_name}|${patient_age}|${severity}|${priority}|${care_units}|${infectious_flag}"
printf '%s\n' "$patient_record" > "$triage_fifo"

echo
print_line
printf '%-14s %-12s %-10s %-10s %-12s %-12s %-12s\n' \
    "name" "age" "severity" "priority" "care_units" "infectious" "bed_hint"
print_line
printf '%-14s %-12s %-10s %-10s %-12s %-12s %-12s\n' \
    "$patient_name" "$patient_age" "$severity" "$priority" "$care_units" "$infectious_text" "$expected_bed"
print_line
echo "status: patient sent to admissions queue"
echo
