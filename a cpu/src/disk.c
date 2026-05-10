#include "../include/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void escape_value(const char *src, char *dst, size_t cap) {
    size_t j = 0;
    if (cap == 0) return;
    for (size_t i = 0; src[i] != '\0' && j + 1 < cap; i++) {
        char c = src[i];
        if ((c == '\\' || c == '\n' || c == '\r') && j + 2 < cap) {
            dst[j++] = '\\';
            dst[j++] = (c == '\n') ? 'n' : (c == '\r' ? 'r' : '\\');
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static void unescape_value(const char *src, char *dst, size_t cap) {
    size_t j = 0;
    if (cap == 0) return;
    for (size_t i = 0; src[i] != '\0' && j + 1 < cap; i++) {
        if (src[i] == '\\' && src[i + 1] != '\0') {
            i++;
            dst[j++] = (src[i] == 'n') ? '\n' : (src[i] == 'r' ? '\r' : src[i]);
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

void disk_persist_process(Process *p) {
    FILE *fp = fopen(p->swapFileName, "w");
    if (!fp) return;
    fprintf(fp, "pc=%d\n", p->programCounter);
    fprintf(fp, "inst=%d\n", p->instructionCount);
    for (int i = 0; i < p->instructionCount; i++) fprintf(fp, "I:%s\n", p->instructions[i]);
    for (int i = 0; i < VAR_SLOTS; i++) {
        char encoded[WORD_SIZE];
        escape_value(p->varValues[i][0] ? p->varValues[i] : "_", encoded, sizeof(encoded));
        fprintf(fp, "V:%s=%s\n", p->varNames[i][0] ? p->varNames[i] : "_", encoded);
    }
    fclose(fp);
}

void disk_reload_process(Process *p) {
    FILE *fp = fopen(p->swapFileName, "r");
    if (!fp) return;

    char line[MAX_LINE];
    int iv = 0;
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (strncmp(line, "pc=", 3) == 0) {
            p->programCounter = atoi(line + 3);
        } else if (strncmp(line, "V:", 2) == 0 && iv < VAR_SLOTS) {
            char *eq = strchr(line + 2, '=');
            if (!eq) continue;
            *eq = '\0';
            if (strcmp(line + 2, "_") == 0) {
                p->varNames[iv][0] = '\0';
                p->varValues[iv][0] = '\0';
            } else {
                strncpy(p->varNames[iv], line + 2, MAX_NAME - 1);
                p->varNames[iv][MAX_NAME - 1] = '\0';
                unescape_value(eq + 1, p->varValues[iv], MAX_VALUE);
            }
            iv++;
        }
    }
    fclose(fp);
}

bool disk_swap_out_one(OS *os, int excludePID) {
    for (int i = 0; i < os->processCount; i++) {
        Process *v = &os->processTable[i];
        if (v->processID == excludePID || v->state == STATE_FINISHED || v->swappedToDisk || v->memoryLowerBound < 0) continue;

        disk_persist_process(v);
        for (int w = v->memoryLowerBound; w <= v->memoryUpperBound && w < MEMORY_WORDS; w++) {
            os->memoryUsed[w] = false;
            os->memoryWords[w][0] = '\0';
        }
        v->memoryLowerBound = -1;
        v->memoryUpperBound = -1;
        v->swappedToDisk = true;
        printf("[SWAP OUT] PID %d -> %s\n", v->processID, v->swapFileName);
        return true;
    }
    return false;
}
