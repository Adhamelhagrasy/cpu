#ifndef OS_H
#define OS_H

#include <stdbool.h>

#define MEMORY_WORDS 40
#define WORD_SIZE 512
#define MAX_PROCESSES 32
#define MAX_INSTRUCTIONS 64
#define MAX_LINE 256
#define MAX_NAME 64
#define MAX_VALUE 160
#define PCB_WORDS 6
#define VAR_SLOTS 3

typedef enum {
    STATE_READY = 0,
    STATE_RUNNING,
    STATE_BLOCKED,
    STATE_FINISHED
} ProcessState;

typedef enum {
    SCHED_RR = 0,
    SCHED_HRRN,
    SCHED_MLFQ
} SchedulerType;

typedef enum {
    EXEC_CONTINUE = 0,
    EXEC_BLOCKED,
    EXEC_ERROR
} ExecResult;

typedef struct QNode {
    int pid;
    struct QNode *next;
} QNode;

typedef struct {
    QNode *front;
    QNode *rear;
} Queue;

typedef struct {
    bool locked;
    int ownerPID;
    Queue blockedQueue;
    const char *name;
} Mutex;

typedef struct {
    int processID;
    ProcessState state;
    int programCounter;
    int memoryLowerBound;
    int memoryUpperBound;
    int instructionCount;
    char instructions[MAX_INSTRUCTIONS][MAX_LINE];
    char varNames[VAR_SLOTS][MAX_NAME];
    char varValues[VAR_SLOTS][MAX_VALUE];
    bool swappedToDisk;
    char swapFileName[64];
    int arrivalTime;
    int readySince;
    int finishedAt;
    int queueLevel;
} Process;

typedef struct {
    char filename[128];
    int arrivalTime;
    bool loaded;
} ProgramArrival;

typedef struct {
    char memoryWords[MEMORY_WORDS][WORD_SIZE];
    bool memoryUsed[MEMORY_WORDS];
    Process processTable[MAX_PROCESSES];
    int processCount;
    int nextPID;
    int clockTick;
    SchedulerType activeScheduler;
    int rrQuantum;
    Queue readyQueue;
    Queue mlfqQueues[3];
    Queue blockedQueue;
    Mutex userInput;
    Mutex userOutput;
    Mutex file;
} OS;

void os_init(OS *os);
void os_run(OS *os);

void queue_init(Queue *q);
bool queue_empty(const Queue *q);
void enqueue(Queue *q, int pid);
int dequeue(Queue *q);
bool queue_remove(Queue *q, int pid);

void trim_newline(char *s);
void trim_space(char *s);
bool streq_icase(const char *a, const char *b);
const char *state_to_str(ProcessState st);
Process *get_process(OS *os, int pid);
Mutex *get_mutex(OS *os, const char *name);

bool ensure_memory_for(OS *os, Process *p, int excludePID);
bool swap_in_if_needed(OS *os, Process *p, int excludePID);
void release_process_memory(OS *os, Process *p);
void disk_persist_process(Process *p);
void disk_reload_process(Process *p);
bool disk_swap_out_one(OS *os, int excludePID);
void write_process_to_memory(OS *os, Process *p);
void store_pcb_fields(OS *os, Process *p);
void set_variable(OS *os, Process *p, const char *name, const char *value);
const char *get_variable(OS *os, Process *p, const char *name);
void print_memory(OS *os);
void release_finished_memory(OS *os);

int create_process(OS *os, const char *programFile, int arrivalTime);
ExecResult execute_instruction(OS *os, Process *p, const char *line);
void run_scheduler(OS *os, SchedulerType sched, int rrQuantum, ProgramArrival *arrivals, int arrivalCount);
void enqueue_ready_process(OS *os, Process *p);

#endif
