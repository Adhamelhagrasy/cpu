#include "../include/os.h"

#include <stdlib.h>

void queue_init(Queue *q) { q->front = q->rear = NULL; }

bool queue_empty(const Queue *q) { return q->front == NULL; }

void enqueue(Queue *q, int pid) {
    QNode *n = (QNode *)malloc(sizeof(QNode));
    if (!n) return;
    n->pid = pid;
    n->next = NULL;
    if (q->rear) {
        q->rear->next = n;
        q->rear = n;
    } else {
        q->front = q->rear = n;
    }
}

int dequeue(Queue *q) {
    if (!q->front) return -1;
    QNode *n = q->front;
    int pid = n->pid;
    q->front = n->next;
    if (!q->front) q->rear = NULL;
    free(n);
    return pid;
}

bool queue_remove(Queue *q, int pid) {
    QNode *prev = NULL;
    QNode *cur = q->front;
    while (cur) {
        if (cur->pid == pid) {
            if (prev) prev->next = cur->next;
            else q->front = cur->next;
            if (q->rear == cur) q->rear = prev;
            free(cur);
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}
