#include <string.h>
#include <stdio.h>
#include "job_queue.h"

int job_queue_init(job_queue_t *q)
{
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;

    if (pthread_mutex_init(&q->mtx, NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }

    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        perror("pthread_cond_init not_empty");
        pthread_mutex_destroy(&q->mtx);
        return -1;
    }

    if (pthread_cond_init(&q->not_full, NULL) != 0) {
        perror("pthread_cond_init not_full");
        pthread_cond_destroy(&q->not_empty);
        pthread_mutex_destroy(&q->mtx);
        return -1;
    }

    return 0;
}

void job_queue_destroy(job_queue_t *q)
{
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->mtx);
}

void job_queue_push(job_queue_t *q, const sensor_msg_t *item)
{
    pthread_mutex_lock(&q->mtx);

    while (q->count == JOB_QUEUE_SIZE) {
        /* fila cheia → produtor bloqueia */
        pthread_cond_wait(&q->not_full, &q->mtx);
    }

    q->buffer[q->tail] = *item;
    q->tail = (q->tail + 1) % JOB_QUEUE_SIZE;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
}

void job_queue_pop(job_queue_t *q, sensor_msg_t *item)
{
    pthread_mutex_lock(&q->mtx);

    while (q->count == 0) {
        /* fila vazia → consumidor bloqueia */
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }

    *item = q->buffer[q->head];
    q->head = (q->head + 1) % JOB_QUEUE_SIZE;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
}
