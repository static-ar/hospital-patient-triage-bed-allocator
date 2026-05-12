# demo commands for vmware ubuntu

sudo apt update
sudo apt install build-essential gcc make valgrind git

unzip hospital_patient_triage_bed_allocator.zip
cd hospital_patient_triage_bed_allocator

make all

./scripts/start_hospital.sh best
./scripts/triage.sh ali 20 9
./scripts/triage.sh sara 22 5 yes
./scripts/triage.sh hamza 30 2

# optional stress test
./scripts/stress_test.sh

# watch logs in another terminal
tail -f logs/hospital_runtime.log
cat logs/schedule_log.txt
cat logs/memory_log.txt
strings logs/patient_records.dat

./scripts/stop_hospital.sh

# compare strategies
./scripts/start_hospital.sh first
./scripts/stress_test.sh
sleep 20
./scripts/stop_hospital.sh

./scripts/start_hospital.sh worst
./scripts/stress_test.sh
sleep 20
./scripts/stop_hospital.sh
