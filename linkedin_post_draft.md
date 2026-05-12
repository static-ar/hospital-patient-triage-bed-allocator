from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import PageBreak, Paragraph, Preformatted, SimpleDocTemplate, Spacer, Table, TableStyle

root = Path(__file__).resolve().parents[1]
styles = getSampleStyleSheet()
styles.add(ParagraphStyle(name="small", parent=styles["BodyText"], fontSize=8, leading=10))
styles.add(ParagraphStyle(name="tiny", parent=styles["BodyText"], fontSize=7, leading=8))
styles.add(ParagraphStyle(name="code_small", parent=styles["Code"], fontSize=8, leading=10))

members = [
    "Muhammad Talha (23F-0511)",
    "Abdul Rafay (23F-0591)",
    "Masooma Mirza (23F-0876)",
]

contribution_rows = [
    ["phase / module", members[0], members[1], members[2]],
    ["shell scripts", "reviewed run flow", "triage, start, stop, stress scripts", "tested script cases"],
    ["makefile", "checked build targets", "makefile all, clean, run, test", "tested clean/build cycle"],
    ["process management", "scheduler-side process start flow", "fork, execv, sigchld, waitpid(wnohang), patient lifecycle", "tested concurrent process cases"],
    ["ipc", "used ipc in scheduler flow", "triage fifo, discharge fifo, shared memory setup/cleanup", "verified shared ward state during memory tests"],
    ["scheduling", "priority queue, fcfs, priority scheduling, schedule log", "helped with patient input test data", "verified output metrics"],
    ["threads", "receptionist, scheduler, nurse, discharge listener threads", "supported integration testing", "verified nurse discharge behavior"],
    ["mutex and condition variables", "bed lock, queue lock, bed_freed_cond", "helped debug shutdown cases", "verified no allocation/free conflict in stress run"],
    ["semaphores", "icu/isolation semaphores, bounded producer-consumer queue", "tested blocking/release scenario", "checked semaphore effect on ward state"],
    ["memory allocator", "integrated allocator with scheduler", "tested patient admission path", "best-fit, first-fit, worst-fit, coalescing"],
    ["fragmentation and paging", "logged paging output during admissions", "tested logs after discharge", "external fragmentation, internal fragmentation, page table"],
    ["report and video", "report finalization, contribution table, demo video script and voice-over", "screenshots and run commands support", "memory section and testing notes"],
]


def p(text, style="BodyText"):
    safe = str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return Paragraph(safe, styles[style])


def table_cell(text):
    return p(text, "tiny")


def add_title(story, title, subtitle=None):
    story.append(Paragraph(title, styles["Title"]))
    if subtitle:
        story.append(Paragraph(subtitle, styles["BodyText"]))
    story.append(Spacer(1, 0.18 * inch))


def add_heading(story, text):
    story.append(Spacer(1, 0.09 * inch))
    story.append(Paragraph(text, styles["Heading2"]))


def add_para(story, text):
    story.append(Paragraph(text, styles["BodyText"]))
    story.append(Spacer(1, 0.07 * inch))


def add_code(story, text):
    story.append(Preformatted(text, styles["code_small"]))
    story.append(Spacer(1, 0.07 * inch))


def styled_table(data, col_widths, font_size=8):
    converted = []
    for row_index, row in enumerate(data):
        converted_row = []
        for item in row:
            converted_row.append(table_cell(item) if row_index > 0 else p(item, "small"))
        converted.append(converted_row)
    table = Table(converted, colWidths=col_widths, repeatRows=1)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
        ("GRID", (0, 0), (-1, -1), 0.25, colors.grey),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("FONTSIZE", (0, 0), (-1, -1), font_size),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ]))
    return table


def make_report():
    pdf_path = root / "report.pdf"
    doc = SimpleDocTemplate(str(pdf_path), pagesize=A4, rightMargin=42, leftMargin=42, topMargin=42, bottomMargin=42)
    story = []

    add_title(story, "hospital patient triage and bed allocator", "cl2006 operating systems lab - final report")
    add_para(story, "group: group xx")
    add_para(story, "members: Muhammad Talha (23F-0511), Abdul Rafay (23F-0591), Masooma Mirza (23F-0876)")
    add_para(story, "environment: ubuntu linux inside vmware | language: c with posix apis | compiler: gcc -Wall -Wextra -pthread")
    story.append(Spacer(1, 0.15 * inch))

    add_heading(story, "1. introduction")
    add_para(story, "this project implements a linux-based hospital emergency-room simulator. patients arrive through a triage shell script, receive a priority based on severity, and are allocated to icu, isolation, or general care. the system demonstrates process management, inter-process communication, cpu scheduling, synchronization, semaphores, memory allocation, fragmentation, and paging.")

    add_heading(story, "2. objectives")
    add_para(story, "the objectives are to automate the project using shell scripts and a makefile, create patient processes using fork and execv, communicate through fifos and shared memory, schedule patients by emergency priority, protect shared resources using pthread synchronization, enforce capacity limits using semaphores, and compare best-fit, first-fit, and worst-fit memory allocation.")

    add_heading(story, "3. system design")
    add_code(story, "triage.sh -> /tmp/triage_fifo -> receptionist thread -> bounded priority queue\npriority queue -> scheduler thread -> shared ward memory -> fork + execv patient_simulator\npatient_simulator -> /tmp/discharge_fifo -> discharge listener -> nurse queue -> nurse threads\nnurse threads -> free bed -> coalesce -> log fragmentation -> signal scheduler")

    add_heading(story, "4. phase 1: environment setup and shell scripting")
    add_para(story, "triage.sh validates name, age, severity, and optional infectious flag. it computes triage priority and writes the patient record to /tmp/triage_fifo. start_hospital.sh builds the project, removes stale ipc files, starts admissions in the background, and prints ward capacity. stop_hospital.sh sends sigterm, waits for shutdown, removes fifos/shared memory/semaphores, and prints recent log lines. stress_test.sh sends 20 rapid patient arrivals. the makefile provides all, clean, run, test, stop, and valgrind targets.")

    add_heading(story, "5. phase 2a and 2b: process management and ipc")
    add_para(story, "the admissions scheduler calls fork for every admitted patient. the child uses execv to replace itself with build/patient_simulator and receives patient id, priority, partition id, bed type, and care units as arguments. the parent stores the child pid. a sigchld handler uses waitpid(-1, null, wnohang) to reap zombie processes without blocking the main system.")
    add_para(story, "ipc is demonstrated with /tmp/triage_fifo for patient arrivals, /tmp/discharge_fifo for patient discharge notifications, posix shared memory /hospital_bed_shm for the ward state, and named semaphores /sem_icu_limit and /sem_isolation_limit for capacity control.")

    add_heading(story, "6. phase 2c: scheduling")
    add_para(story, "patients wait inside a bounded priority queue. lower priority number means more serious emergency, so priority 1 is selected before priority 2, and so on. schedule_log.txt records live admission events and also writes fcfs and priority scheduling simulations with gantt-style output, average waiting time, and average turnaround time.")

    add_heading(story, "7. phase 3: threads and synchronization")
    thread_table = [
        ["thread role", "responsibility"],
        ["receptionist", "reads triage fifo and produces patient records"],
        ["scheduler", "consumes patients, allocates beds, and starts patient processes"],
        ["nurse-icu", "handles icu discharge, frees partition, releases icu semaphore"],
        ["nurse-isolation", "handles isolation discharge, frees partition, releases isolation semaphore"],
        ["nurse-general", "handles general discharge and frees partition"],
        ["discharge listener", "reads discharge fifo and routes patient ids to nurse queues"],
    ]
    story.append(styled_table(thread_table, [1.5 * inch, 4.7 * inch]))
    story.append(Spacer(1, 0.12 * inch))
    add_para(story, "bed_lock protects shared ward state. bed_freed_cond wakes the scheduler when a nurse frees a bed. separate queue locks and condition variables protect the patient queue. counting semaphores enforce icu and isolation limits. bounded semaphores demonstrate producer-consumer synchronization between the receptionist and scheduler.")

    add_heading(story, "8. phase 4: memory management")
    add_para(story, "the ward is modeled as 32 contiguous care units: 4 icu beds x 3 units, 4 isolation beds x 2 units, and 12 general beds x 1 unit. the allocation strategy is selected at runtime using --strategy best, --strategy first, or --strategy worst. best-fit selects the smallest suitable partition, first-fit selects the first suitable partition, and worst-fit selects the largest suitable partition.")
    add_para(story, "on discharge, adjacent free partitions are coalesced. after every allocation/deallocation event, memory_log.txt records total free units, largest free block, external fragmentation percentage, and active patient count.")
    add_code(story, "external fragmentation = (1 - largest_free_block / total_free_units) * 100")
    add_para(story, "the paging simulation uses page size 2 care units. internal fragmentation is reported when a patient's care units are not a multiple of the page size. the bonus mmap log writes admission and discharge records into logs/patient_records.dat.")

    story.append(PageBreak())
    add_heading(story, "9. testing plan")
    test_rows = [
        ["test", "command", "expected result"],
        ["build", "make all", "zero warnings"],
        ["start", "./scripts/start_hospital.sh best", "admissions starts and prints capacity"],
        ["single patient", "./scripts/triage.sh ali 20 8", "patient admitted, treated, and discharged"],
        ["invalid input", "./scripts/triage.sh ali x 8", "validation error"],
        ["stress", "./scripts/stress_test.sh", "20 arrivals, no crash"],
        ["strategy", "./scripts/start_hospital.sh worst", "worst-fit allocator used"],
        ["stop", "./scripts/stop_hospital.sh", "ipc cleanup"],
    ]
    story.append(styled_table(test_rows, [1.0 * inch, 2.2 * inch, 3.0 * inch]))

    add_heading(story, "10. valgrind")
    add_para(story, "run valgrind inside vmware ubuntu and paste a screenshot in the final submitted report. command: valgrind --leak-check=full --show-leak-kinds=all ./build/admissions --strategy best")

    add_heading(story, "11. challenges and lessons learned")
    add_para(story, "important challenges were avoiding race conditions on the ward map, preventing zombie processes, making fifo reads cooperate with shutdown, demonstrating semaphore blocking and release, and handling coalescing correctly after patient discharge.")

    add_heading(story, "12. individual contribution table")
    story.append(styled_table(contribution_rows, [1.35 * inch, 1.55 * inch, 1.65 * inch, 1.65 * inch], font_size=7))

    add_heading(story, "13. conclusion")
    add_para(story, "the final system combines multiple operating systems concepts into one working linux application. it demonstrates process creation, inter-process communication, thread synchronization, cpu scheduling, memory allocation, fragmentation reporting, paging simulation, and clean shutdown using a hospital triage scenario.")

    doc.build(story)


def make_self_eval():
    pdf_path = root / "self_eval.pdf"
    doc = SimpleDocTemplate(str(pdf_path), pagesize=A4, rightMargin=42, leftMargin=42, topMargin=42, bottomMargin=42)
    story = []

    add_title(story, "semester project self-evaluation form", "hospital patient triage and bed allocator")
    add_para(story, "section: ________ | group: group xx | instructor: hassan ahmed")
    add_para(story, f"student 1: {members[0]}")
    add_para(story, f"student 2: {members[1]}")
    add_para(story, f"student 3: {members[2]}")
    add_para(story, "github repository url: ______________________________________________")

    checklist = [
        "triage.sh input validation, priority mapping, fifo output",
        "start_hospital.sh launches admissions and prints capacity",
        "stop_hospital.sh cleans fifos, shared memory, and semaphores",
        "makefile all, clean, run, test targets",
        "fork and execv per patient admission",
        "sigchld handler and waitpid(wnohang)",
        "patient simulator lifecycle messages and treatment sleep",
        "10+ concurrent patient processes handled",
        "triage fifo and discharge fifo ipc",
        "shared memory ward state",
        "priority queue admission order",
        "fcfs and priority scheduling logs",
        "receptionist, scheduler, and nurse threads",
        "mutex protects ward bitmap",
        "condition variable signals bed availability",
        "icu and isolation semaphores",
        "bounded producer-consumer patient queue",
        "best-fit, first-fit, worst-fit",
        "coalescing adjacent free partitions",
        "external fragmentation logging",
        "paging and internal fragmentation report",
        "mmap patient record log bonus",
        "valgrind screenshot added before final submission",
        "demo video recorded in vmware with voice-over",
    ]

    add_heading(story, "implementation checklist")
    check_rows = [["status", "module"]]
    for item in checklist:
        check_rows.append(["[  ]", item])
    story.append(styled_table(check_rows, [0.6 * inch, 5.6 * inch], font_size=8))
    story.append(PageBreak())

    add_heading(story, "individual contributions")
    story.append(styled_table(contribution_rows, [1.35 * inch, 1.55 * inch, 1.65 * inch, 1.65 * inch], font_size=7))

    add_heading(story, "specific task summary")
    add_para(story, f"{members[0]}: scheduling, threads, synchronization, semaphores, report finalization, and demo video explanation.")
    add_para(story, f"{members[1]}: shell scripts, makefile, process management, fifo/shared-memory ipc, and patient process lifecycle.")
    add_para(story, f"{members[2]}: memory allocator, coalescing, fragmentation reporting, paging simulation, testing notes, and readme support.")

    add_heading(story, "reflection")
    add_para(story, "1. synchronization challenge: scheduler and nurse threads can access the ward at the same time, so bed_lock and condition variables were used.")
    add_para(story, "2. memory allocation trade-offs: best-fit saves space in many cases but can create small free blocks, so coalescing is required.")
    add_para(story, "3. known bugs or unfinished modules: add any issue discovered during final vmware testing here.")

    add_heading(story, "portfolio links")
    portfolio = [
        ["platform", members[0], members[1], members[2]],
        ["github", "", "", ""],
        ["linkedin", "", "", ""],
        ["medium", "", "", ""],
    ]
    story.append(styled_table(portfolio, [1.0 * inch, 1.75 * inch, 1.75 * inch, 1.75 * inch], font_size=7))

    add_heading(story, "signatures")
    add_para(story, f"{members[0]} signature: __________________ date: __________")
    add_para(story, f"{members[1]} signature: __________________ date: __________")
    add_para(story, f"{members[2]} signature: __________________ date: __________")

    doc.build(story)


if __name__ == "__main__":
    make_report()
    make_self_eval()
    print("generated report.pdf and self_eval.pdf")
