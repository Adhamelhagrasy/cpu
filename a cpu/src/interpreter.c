#include "../include/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool queue_contains_pid(const Queue *q, int pid) {
    for (const QNode *cur = q->front; cur; cur = cur->next) {
        if (cur->pid == pid) return true;
    }
    return false;
}

static const char *resolve(OS *os, Process *p, const char *token, char *buf, size_t n) {
    const char *v = get_variable(os, p, token);
    if (v) {
        strncpy(buf, v, n - 1);
        buf[n - 1] = '\0';
        return buf;
    }
    return token;
}

/** Read full file into buf (NUL-terminated), up to cap-1 bytes. */
static void read_file_contents(FILE *fp, char *buf, size_t cap) {
    size_t n = fread(buf, 1, cap - 1, fp);
    buf[n] = '\0';
    trim_newline(buf);
}

ExecResult execute_instruction(OS *os, Process *p, const char *line) {
    char work[MAX_LINE];
    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char *cmd = strtok(work, " ");
    if (!cmd) return EXEC_CONTINUE;

    if (strcmp(cmd, "print") == 0) {
        char *x = strtok(NULL, " ");
        if (!x) return EXEC_ERROR;
        const char *v = get_variable(os, p, x);
        printf("[PID %d] %s\n", p->processID, v ? v : x);
        return EXEC_CONTINUE;
    }

    if (strcmp(cmd, "assign") == 0) {
        char *x = strtok(NULL, " ");
        char *y = strtok(NULL, "");
        if (!x || !y) return EXEC_ERROR;
        trim_space(y);

        if (strcmp(y, "input") == 0) {
            char inp[MAX_VALUE];
            printf("[PID %d] input for %s:\n", p->processID, x);
            fflush(stdout);
            if (!fgets(inp, sizeof(inp), stdin)) inp[0] = '\0';
            trim_newline(inp);
            set_variable(os, p, x, inp);
            return EXEC_CONTINUE;
        }

        if (strncmp(y, "readFile ", 9) == 0) {
            char arg[MAX_VALUE], fileName[MAX_VALUE], content[MAX_VALUE] = {0};
            strncpy(arg, y + 9, sizeof(arg) - 1);
            arg[sizeof(arg) - 1] = '\0';
            resolve(os, p, arg, fileName, sizeof(fileName));
            FILE *fp = fopen(fileName, "r");
            if (fp) {
                read_file_contents(fp, content, sizeof(content));
                fclose(fp);
            }
            set_variable(os, p, x, content);
            return EXEC_CONTINUE;
        }

        char value[MAX_VALUE];
        set_variable(os, p, x, resolve(os, p, y, value, sizeof(value)));
        return EXEC_CONTINUE;
    }

    if (strcmp(cmd, "writeFile") == 0) {
        char *x = strtok(NULL, " ");
        char *y = strtok(NULL, "");
        if (!x || !y) return EXEC_ERROR;
        trim_space(y);
        char fname[MAX_VALUE], text[MAX_VALUE];
        resolve(os, p, x, fname, sizeof(fname));
        resolve(os, p, y, text, sizeof(text));
        FILE *fp = fopen(fname, "w");
        if (!fp) return EXEC_ERROR;
        fputs(text, fp);
        fclose(fp);
        return EXEC_CONTINUE;
    }

    if (strcmp(cmd, "readFile") == 0) {
        char *x = strtok(NULL, " ");
        if (!x) return EXEC_ERROR;
        char file[MAX_VALUE], content[MAX_VALUE] = {0};
        resolve(os, p, x, file, sizeof(file));
        FILE *fp = fopen(file, "r");
        if (fp) {
            read_file_contents(fp, content, sizeof(content));
            fclose(fp);
        }
        set_variable(os, p, "_lastRead", content);
        return EXEC_CONTINUE;
    }

    if (strcmp(cmd, "printFromTo") == 0) {
        char *x = strtok(NULL, " ");
        char *y = strtok(NULL, " ");
        if (!x || !y) return EXEC_ERROR;
        char ax[MAX_VALUE], by[MAX_VALUE];
        int a = atoi(resolve(os, p, x, ax, sizeof(ax)));
        int b = atoi(resolve(os, p, y, by, sizeof(by)));
        printf("[PID %d] ", p->processID);
        if (a <= b) for (int i = a; i <= b; i++) printf("%d ", i);
        else for (int i = a; i >= b; i--) printf("%d ", i);
        printf("\n");
        return EXEC_CONTINUE;
    }

    if (strcmp(cmd, "semWait") == 0) {
        char *res = strtok(NULL, " ");
        if (!res) return EXEC_ERROR;
        Mutex *m = get_mutex(os, res);
        if (!m) return EXEC_ERROR;

        // If process already owns this mutex, keep running without blocking.
        if (m->locked && m->ownerPID == p->processID) return EXEC_CONTINUE;

        if (!m->locked) {
            m->locked = true;
            m->ownerPID = p->processID;
            return EXEC_CONTINUE;
        }

        p->state = STATE_BLOCKED;
        if (!queue_contains_pid(&m->blockedQueue, p->processID)) enqueue(&m->blockedQueue, p->processID);
        if (!queue_contains_pid(&os->blockedQueue, p->processID)) enqueue(&os->blockedQueue, p->processID);
        store_pcb_fields(os, p);
        return EXEC_BLOCKED;
    }

    if (strcmp(cmd, "semSignal") == 0) {
        char *res = strtok(NULL, " ");
        if (!res) return EXEC_ERROR;
        Mutex *m = get_mutex(os, res);
        if (!m || m->ownerPID != p->processID) return EXEC_CONTINUE;

        if (queue_empty(&m->blockedQueue)) {
            m->locked = false;
            m->ownerPID = -1;
            return EXEC_CONTINUE;
        }
        int nxt = dequeue(&m->blockedQueue);
        queue_remove(&os->blockedQueue, nxt);
        Process *up = get_process(os, nxt);
        if (up && up->state != STATE_FINISHED) {
            up->state = STATE_READY;
            up->readySince = os->clockTick;
            store_pcb_fields(os, up);
            enqueue_ready_process(os, up);
            m->locked = true;
            m->ownerPID = nxt;
        }
        return EXEC_CONTINUE;
    }

    return EXEC_ERROR;
}
