#include "../include/os.h"

#include <ctype.h>
#include <string.h>

void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

void trim_space(char *s) {
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
    int end = (int)strlen(s) - 1;
    while (end >= 0 && isspace((unsigned char)s[end])) s[end--] = '\0';
}

bool streq_icase(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (tolower(ca) != tolower(cb)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

const char *state_to_str(ProcessState st) {
    switch (st) {
        case STATE_READY: return "READY";
        case STATE_RUNNING: return "RUNNING";
        case STATE_BLOCKED: return "BLOCKED";
        case STATE_FINISHED: return "FINISHED";
        default: return "UNKNOWN";
    }
}

Process *get_process(OS *os, int pid) {
    for (int i = 0; i < os->processCount; i++) {
        if (os->processTable[i].processID == pid) return &os->processTable[i];
    }
    return NULL;
}

Mutex *get_mutex(OS *os, const char *name) {
    if (strcmp(name, "userInput") == 0) return &os->userInput;
    if (strcmp(name, "userOutput") == 0) return &os->userOutput;
    if (strcmp(name, "file") == 0) return &os->file;
    return NULL;
}
