#include "../include/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void os_init(OS *os) {
    memset(os, 0, sizeof(*os));
    os->nextPID = 1;
    os->activeScheduler = SCHED_RR;
    os->rrQuantum = 2;
    queue_init(&os->readyQueue);
    for (int i = 0; i < 3; i++) queue_init(&os->mlfqQueues[i]);
    queue_init(&os->blockedQueue);

    os->userInput.name = "userInput";
    os->userOutput.name = "userOutput";
    os->file.name = "file";
    os->userInput.ownerPID = os->userOutput.ownerPID = os->file.ownerPID = -1;
    queue_init(&os->userInput.blockedQueue);
    queue_init(&os->userOutput.blockedQueue);
    queue_init(&os->file.blockedQueue);
}

void os_run(OS *os) {
    char schedBuf[32];
    ProgramArrival arrivals[MAX_PROCESSES];
    int count = 0;
    int quantum = 2;

    printf("Scheduler (RR/HRRN/MLFQ): ");
    if (!fgets(schedBuf, sizeof(schedBuf), stdin)) return;
    trim_newline(schedBuf);
    trim_space(schedBuf);
    SchedulerType sched = SCHED_RR;
    if (streq_icase(schedBuf, "HRRN")) {
        sched = SCHED_HRRN;
    } else if (streq_icase(schedBuf, "MLFQ")) {
        sched = SCHED_MLFQ;
    } else if (streq_icase(schedBuf, "RR")) {
        sched = SCHED_RR;
    } else {
        printf("Unknown scheduler '%s' (defaulting to RR)\n", schedBuf);
        sched = SCHED_RR;
    }

    if (sched == SCHED_RR) {
        char q[32];
        printf("RR quantum (default 2): ");
        if (fgets(q, sizeof(q), stdin)) {
            int parsed = atoi(q);
            if (parsed > 0) quantum = parsed;
        }
    }

    printf("Number of program files: ");
    char n[32];
    if (!fgets(n, sizeof(n), stdin)) return;
    count = atoi(n);
    if (count < 1 || count > MAX_PROCESSES) return;

    for (int i = 0; i < count; i++) {
        printf("Program %d filename: ", i + 1);
        if (!fgets(arrivals[i].filename, sizeof(arrivals[i].filename), stdin)) return;
        trim_newline(arrivals[i].filename);
        trim_space(arrivals[i].filename);
        printf("Program %d arrival time: ", i + 1);
        char t[32];
        if (!fgets(t, sizeof(t), stdin)) return;
        arrivals[i].arrivalTime = atoi(t);
        arrivals[i].loaded = false;
    }

    run_scheduler(os, sched, quantum, arrivals, count);
    print_memory(os);
    release_finished_memory(os);
    printf("\nSimulation finished at tick %d\n", os->clockTick);
}
