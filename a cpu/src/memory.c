#include "../include/os.h"

#include <stdio.h>
#include <string.h>

static int block_size(const Process *p) { return PCB_WORDS + p->instructionCount + VAR_SLOTS; }

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

static bool find_free_contiguous(OS *os, int size, int *lo, int *hi) {
    for (int i = 0; i <= MEMORY_WORDS - size; i++) {
        bool ok = true;
        for (int j = i; j < i + size; j++) {
            if (os->memoryUsed[j]) {
                ok = false;
                i = j;
                break;
            }
        }
        if (ok) {
            *lo = i;
            *hi = i + size - 1;
            return true;
        }
    }
    return false;
}

static void read_variables(OS *os, Process *p) {
    int base = p->memoryLowerBound + PCB_WORDS + p->instructionCount;
    for (int i = 0; i < VAR_SLOTS; i++) {
        char tag[16], n[MAX_NAME], encoded[WORD_SIZE], row[WORD_SIZE];
        strncpy(row, os->memoryWords[base + i], sizeof(row) - 1);
        row[sizeof(row) - 1] = '\0';
        if (sscanf(row, "%15s %63s %511[^\n]", tag, n, encoded) == 3 && strcmp(tag, "var") == 0 && strcmp(n, "_") != 0) {
            strncpy(p->varNames[i], n, MAX_NAME - 1);
            p->varNames[i][MAX_NAME - 1] = '\0';
            unescape_value(encoded, p->varValues[i], MAX_VALUE);
        } else {
            p->varNames[i][0] = '\0';
            p->varValues[i][0] = '\0';
        }
    }
}

static void write_variables(OS *os, Process *p) {
    int base = p->memoryLowerBound + PCB_WORDS + p->instructionCount;
    for (int i = 0; i < VAR_SLOTS; i++) {
        if (p->varNames[i][0] == '\0') snprintf(os->memoryWords[base + i], WORD_SIZE, "var _ _");
        else {
            char encoded[WORD_SIZE];
            escape_value(p->varValues[i], encoded, sizeof(encoded));
            snprintf(os->memoryWords[base + i], WORD_SIZE, "var %s %s", p->varNames[i], encoded);
        }
    }
}

bool ensure_memory_for(OS *os, Process *p, int excludePID) {
    int lo, hi;
    int need = block_size(p);
    while (!find_free_contiguous(os, need, &lo, &hi)) {
        if (!disk_swap_out_one(os, excludePID)) return false;
    }
    p->memoryLowerBound = lo;
    p->memoryUpperBound = hi;
    for (int i = lo; i <= hi; i++) os->memoryUsed[i] = true;
    return true;
}

bool swap_in_if_needed(OS *os, Process *p, int excludePID) {
    if (!p->swappedToDisk) return true;
    if (!ensure_memory_for(os, p, excludePID)) return false;
    disk_reload_process(p);
    write_process_to_memory(os, p);
    p->swappedToDisk = false;
    printf("[SWAP IN ] PID %d <- %s\n", p->processID, p->swapFileName);
    return true;
}

void release_process_memory(OS *os, Process *p) {
    if (p->memoryLowerBound < 0 || p->memoryUpperBound < 0) return;
    for (int i = p->memoryLowerBound; i <= p->memoryUpperBound && i < MEMORY_WORDS; i++) {
        os->memoryUsed[i] = false;
        os->memoryWords[i][0] = '\0';
    }
    p->memoryLowerBound = -1;
    p->memoryUpperBound = -1;
}

void write_process_to_memory(OS *os, Process *p) {
    int lo = p->memoryLowerBound;
    snprintf(os->memoryWords[lo], WORD_SIZE, "processID %d", p->processID);
    snprintf(os->memoryWords[lo + 1], WORD_SIZE, "state %s", state_to_str(p->state));
    snprintf(os->memoryWords[lo + 2], WORD_SIZE, "programCounter %d", p->programCounter);
    snprintf(os->memoryWords[lo + 3], WORD_SIZE, "memoryLowerBound %d", p->memoryLowerBound);
    snprintf(os->memoryWords[lo + 4], WORD_SIZE, "memoryUpperBound %d", p->memoryUpperBound);
    snprintf(os->memoryWords[lo + 5], WORD_SIZE, "instructionCount %d", p->instructionCount);
    int idx = lo + PCB_WORDS;
    for (int i = 0; i < p->instructionCount; i++) snprintf(os->memoryWords[idx++], WORD_SIZE, "inst %s", p->instructions[i]);
    write_variables(os, p);
}

void store_pcb_fields(OS *os, Process *p) {
    if (p->memoryLowerBound < 0 || p->memoryUpperBound < 0) return;
    snprintf(os->memoryWords[p->memoryLowerBound + 1], WORD_SIZE, "state %s", state_to_str(p->state));
    snprintf(os->memoryWords[p->memoryLowerBound + 2], WORD_SIZE, "programCounter %d", p->programCounter);
}

void set_variable(OS *os, Process *p, const char *name, const char *value) {
    read_variables(os, p);
    for (int i = 0; i < VAR_SLOTS; i++) {
        if (strcmp(p->varNames[i], name) == 0 || p->varNames[i][0] == '\0') {
            strncpy(p->varNames[i], name, MAX_NAME - 1);
            p->varNames[i][MAX_NAME - 1] = '\0';
            strncpy(p->varValues[i], value, MAX_VALUE - 1);
            p->varValues[i][MAX_VALUE - 1] = '\0';
            write_variables(os, p);
            return;
        }
    }
    strncpy(p->varNames[VAR_SLOTS - 1], name, MAX_NAME - 1);
    p->varNames[VAR_SLOTS - 1][MAX_NAME - 1] = '\0';
    strncpy(p->varValues[VAR_SLOTS - 1], value, MAX_VALUE - 1);
    p->varValues[VAR_SLOTS - 1][MAX_VALUE - 1] = '\0';
    write_variables(os, p);
}

const char *get_variable(OS *os, Process *p, const char *name) {
    read_variables(os, p);
    for (int i = 0; i < VAR_SLOTS; i++) {
        if (strcmp(p->varNames[i], name) == 0) return p->varValues[i];
    }
    return NULL;
}

void print_memory(OS *os) {
    printf("\n=== MEMORY DUMP ===\n");
    for (int i = 0; i < MEMORY_WORDS; i++) {
        printf("[%02d] %s\n", i, os->memoryUsed[i] ? os->memoryWords[i] : "<FREE>");
    }
}
