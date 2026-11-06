#include <stdlib.h>
#include <string.h>
#include "mycp.h"

void queue_init(Queue *q)
{
    q->head  = NULL;
    q->tail  = NULL;
    q->count = 0;
}

/* Append a task at the tail. Returns 0 on success, -1 on allocation failure. */
int queue_push(Queue *q, const char *src, const char *dst,
               off_t size, time_t mtime, mode_t mode)
{
    Task *t = calloc(1, sizeof *t);

    if (!t)
        return -1;
    t->src = strdup(src);
    t->dst = strdup(dst);
    if (!t->src || !t->dst) {
        task_free(t);
        return -1;
    }
    t->size  = size;
    t->mtime = mtime;
    t->mode  = mode;
    t->next  = NULL;

    if (q->tail)
        q->tail->next = t;
    else
        q->head = t;
    q->tail = t;
    q->count++;
    return 0;
}

/* Remove and return the head task (caller frees it), or NULL if empty. */
Task *queue_pop(Queue *q)
{
    Task *t = q->head;

    if (!t)
        return NULL;
    q->head = t->next;
    if (!q->head)
        q->tail = NULL;
    t->next = NULL;
    q->count--;
    return t;
}

void task_free(Task *t)
{
    if (!t)
        return;
    free(t->src);
    free(t->dst);
    free(t);
}

void queue_clear(Queue *q)
{
    Task *t;

    while ((t = queue_pop(q)) != NULL)
        task_free(t);
}
