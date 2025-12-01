#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <semaphore.h>
#include <time.h>

/* Estrutura armazenada na memória compartilhada */
typedef struct {
    sem_t  sem;        /* semáforo para coordenar acesso entre processos   */
    int    last_value; /* último valor de sensor processado                */
    time_t last_ts;    /* timestamp do último valor                        */
} shared_data_t;

#endif /* SHARED_DATA_H */
