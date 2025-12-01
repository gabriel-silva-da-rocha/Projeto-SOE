#ifndef COMMON_H
#define COMMON_H

#include <time.h>

/* Nome da fila POSIX de mensagens para logs */
#define MQ_NAME  "/sysmon_log"

/* Nome do segmento de memória compartilhada */
#define SHM_NAME "/sysmon_shared"

/* Mensagem básica de sensor enviada pelo processo coletor */
typedef struct {
    int    id;         /* ID do sensor */
    int    value;      /* valor medido */
    time_t timestamp;  /* instante da medição */
} sensor_msg_t;

#endif /* COMMON_H */
