#include "../include/os.h"

#include <stdio.h>
#include <string.h>

static const int MLFQ_LEVELS = 3;
static const int MLFQ_QUANTA[3] = {1, 2, 4};

static bool all_finished(OS *os) {
    if (os->processCount == 0) return false;
    for (int i = 0; i < os->processCount; i++) if (os->processTable[i].state != STATE_FINISHED) return false;
    return true;
}

static bool arrivals_pending(ProgramArrival *arrivals, int n) {
    for (int i = 0; i < n; i++) if (!arrivals[i].loaded) return true;
    return false;
}

static void handle_arrivals(OS *os, ProgramArrival *arrivals, int n) {
    for (int i = 0; i < n; i++) {
        if (!arrivals[i].loaded && arrivals[i].arrivalTime <= os->clockTick) {
            if (create_process(os, arrivals[i].filename, arrivals[i].arrivalTime) != -1) arrivals[i].loaded = true;
        }
    }
}

static int pick_hrrn(OS *os) {
    int bestPID = -1;
    double bestRatio = -1.0;
    for (QNode *cur = os->readyQueue.front; cur; cur = cur->next) {
        Process *p = get_process(os, cur->pid);
        if (!p || p->state != STATE_READY) continue;
        int wait = os->clockTick - p->readySince;
        int burst = p->instructionCount;
        if (burst <= 0) burst = 1;
        double rr = ((double)wait + (double)burst) / (double)burst;
        if (rr > bestRatio) {
            bestRatio = rr;
            bestPID = p->processID;
        }
    }
    if (bestPID != -1) queue_remove(&os->readyQueue, bestPID);
    return bestPID;
}

void enqueue_ready_process(OS *os, Process *p) {
    if (!p || p->state == STATE_FINISHED) return;
    if (os->activeScheduler == SCHED_MLFQ) {
        if (p->queueLevel < 0) p->queueLevel = 0;
        if (p->queueLevel >= MLFQ_LEVELS) p->queueLevel = MLFQ_LEVELS - 1;
        enqueue(&os->mlfqQueues[p->queueLevel], p->processID);
        return;
    }
    enqueue(&os->readyQueue, p->processID);
}

static int pick_mlfq(OS *os) {
    for (int level = 0; level < MLFQ_LEVELS; level++) {
        int pid = dequeue(&os->mlfqQueues[level]);
        if (pid != -1) return pid;
    }
    return -1;
}

void run_scheduler(OS *os, SchedulerType sched, int rrQuantum, ProgramArrival *arrivals, int arrivalCount) {
    os->activeScheduler = sched;
    os->rrQuantum = rrQuantum;

    while (true) {
        handle_arrivals(os, arrivals, arrivalCount);
        if (all_finished(os) && !arrivals_pending(arrivals, arrivalCount)) break;

        int pid = -1;
        if (sched == SCHED_HRRN) pid = pick_hrrn(os);
        else if (sched == SCHED_MLFQ) pid = pick_mlfq(os);
        else pid = dequeue(&os->readyQueue);
        if (pid == -1) {
            os->clockTick++;
            continue;
        }

        Process *p = get_process(os, pid);
        if (!p || p->state == STATE_FINISHED) continue;
        if (!swap_in_if_needed(os, p, pid)) {
            p->state = STATE_FINISHED;
            p->finishedAt = os->clockTick;
            release_process_memory(os, p);
            continue;
        }

        p->state = STATE_RUNNING;
        store_pcb_fields(os, p);
        int budget = p->instructionCount + 1;
        if (sched == SCHED_RR) budget = rrQuantum;
        else if (sched == SCHED_MLFQ) budget = MLFQ_QUANTA[p->queueLevel];
        int ranSteps = 0;

        for (int step = 0; step < budget; step++) {
            if (p->programCounter >= p->instructionCount) {
                p->state = STATE_FINISHED;
                p->finishedAt = os->clockTick;
                store_pcb_fields(os, p);
                break;
            }
            const char *inst = p->instructions[p->programCounter++];
            store_pcb_fields(os, p);
            ExecResult r = execute_instruction(os, p, inst);
            ranSteps++;
            os->clockTick++;
            handle_arrivals(os, arrivals, arrivalCount);
            if (r == EXEC_BLOCKED) break;
            if (r == EXEC_ERROR) printf("[PID %d] invalid instruction: %s\n", p->processID, inst);
            if (p->programCounter >= p->instructionCount) {
                p->state = STATE_FINISHED;
                p->finishedAt = os->clockTick;
                store_pcb_fields(os, p);
                break;
            }
        }

        if (p->state == STATE_RUNNING) {
            p->state = STATE_READY;
            p->readySince = os->clockTick;
            if (sched == SCHED_MLFQ && ranSteps >= budget && p->queueLevel < MLFQ_LEVELS - 1) {
                p->queueLevel++;
            }
            store_pcb_fields(os, p);
            enqueue_ready_process(os, p);
        }
    }
}
