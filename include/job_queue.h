#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <pthread.h>
#include "common.h"

/* Tamanho da fila interna de jobs (produtor–consumidor) */
#define JOB_QUEUE_SIZE 16

typedef struct {
    sensor_msg_t buffer[JOB_QUEUE_SIZE];
    int head;
    int tail;
    int count;

    pthread_mutex_t mtx;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} job_queue_t;

/* Inicializa a fila */
int job_queue_init(job_queue_t *q);

/* Destroi a fila */
void job_queue_destroy(job_queue_t *q);

/* Insere um item (bloqueia se a fila estiver cheia) */
void job_queue_push(job_queue_t *q, const sensor_msg_t *item);

/* Remove um item (bloqueia se a fila estiver vazia) */
void job_queue_pop(job_queue_t *q, sensor_msg_t *item);

#endif /* JOB_QUEUE_H */
