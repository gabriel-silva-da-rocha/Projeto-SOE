#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "common.h"

/*
 * Processo filho: gera leituras de sensores e escreve em STDOUT.
 * No servidor, STDOUT foi redirecionado para o lado de escrita do pipe.
 */

int main(void)
{
    sensor_msg_t msg;

    srand((unsigned int)(time(NULL) ^ getpid()));

    while (1) {
        msg.id        = 1;                 /* sensor fictício */
        msg.value     = rand() % 100;      /* 0..99 */
        msg.timestamp = time(NULL);

        ssize_t n = write(STDOUT_FILENO, &msg, sizeof(msg));
        if (n != (ssize_t)sizeof(msg)) {
            perror("sensor_proc write");
            exit(EXIT_FAILURE);
        }

        /* intervalo entre amostras */
        sleep(1);
    }

    return 0;
}
