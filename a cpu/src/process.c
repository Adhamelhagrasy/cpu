#include "../include/os.h"

#include <stdio.h>
#include <string.h>

static int read_program_lines(const char *path, char instructions[][MAX_LINE]) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    int n = 0;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp) && n < MAX_INSTRUCTIONS) {
        trim_newline(line);
        trim_space(line);
        if (line[0] == '\0') continue;
        strncpy(instructions[n], line, MAX_LINE - 1);
        instructions[n][MAX_LINE - 1] = '\0';
        n++;
    }
    fclose(fp);
    return n;
}

int create_process(OS *os, const char *programFile, int arrivalTime) {
    if (os->processCount >= MAX_PROCESSES) return -1;
    Process *p = &os->processTable[os->processCount];
    memset(p, 0, sizeof(*p));
    p->processID = os->nextPID++;
    p->state = STATE_READY;
    p->arrivalTime = arrivalTime;
    p->readySince = os->clockTick;
    p->memoryLowerBound = -1;
    p->memoryUpperBound = -1;
    p->finishedAt = -1;
    p->queueLevel = 0;
    snprintf(p->swapFileName, sizeof(p->swapFileName), "disk_pid_%d.txt", p->processID);

    int c = read_program_lines(programFile, p->instructions);
    if (c < 0) {
        printf("Cannot open %s\n", programFile);
        return -1;
    }
    p->instructionCount = c;
    os->processCount++;

    if (!ensure_memory_for(os, p, -1)) {
        p->state = STATE_FINISHED;
        return p->processID;
    }
    write_process_to_memory(os, p);
    enqueue_ready_process(os, p);
    printf("[ARRIVAL] PID %d from %s bounds [%d, %d]\n", p->processID, programFile, p->memoryLowerBound, p->memoryUpperBound);
    return p->processID;
}
