#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include "common.h"
#include "shared_data.h"

/* Processo separado que apenas lê a memória compartilhada periodicamente */

int main(void)
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (fd < 0) {
        perror("shm_open");
        return EXIT_FAILURE;
    }

    shared_data_t *shm = mmap(NULL, sizeof(shared_data_t),
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    printf("=== Visualizador da memória compartilhada (%s) ===\n",
           SHM_NAME);

    while (1) {
        if (sem_wait(&shm->sem) < 0) {
            perror("sem_wait");
            break;
        }

        int    value = shm->last_value;
        time_t ts    = shm->last_ts;

        sem_post(&shm->sem);

        struct tm *tm_info = localtime(&ts);
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);

        printf("[SHM] Último valor = %d (ts=%s)\n", value, buf);
        fflush(stdout);

        sleep(1);
    }

    return 0;
}
