#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include "common.h"
#include "shared_data.h"
#include "job_queue.h"

/* Número de threads trabalhadoras */
#define NUM_WORKERS 3

/* Descriptor global para envio de logs via MQ */
static mqd_t g_mq_send;

/* Fila interna de jobs (produtor–consumidor) */
static job_queue_t g_queue;

/* Ponteiro para a área de memória compartilhada */
static shared_data_t *g_shm = NULL;

/* Flag para encerramento simples (não tratamos tudo de forma perfeita) */
static volatile sig_atomic_t g_running = 1;

/* ------------------------------------------------------------ */
/* Função helper: envia texto para a fila de mensagens POSIX    */
/* ------------------------------------------------------------ */
static void send_log(const char *text)
{
    if (g_mq_send == (mqd_t)-1) return;

    size_t len = strnlen(text, 127);
    char buf[128];
    memcpy(buf, text, len);
    buf[len] = '\0';

    if (mq_send(g_mq_send, buf, len + 1, 0) < 0) {
        perror("mq_send");
    }
}

/* ------------------------------------------------------------ */
/* Thread do logger: recebe mensagens da fila e imprime         */
/* ------------------------------------------------------------ */
static void *logger_thread(void *arg)
{
    (void)arg;

    struct mq_attr attr;
    mqd_t mq_recv = mq_open(MQ_NAME, O_RDONLY);
    if (mq_recv == (mqd_t)-1) {
        perror("mq_open logger");
        return NULL;
    }

    if (mq_getattr(mq_recv, &attr) < 0) {
        perror("mq_getattr");
        mq_close(mq_recv);
        return NULL;
    }

    long msgsize = attr.mq_msgsize;
    char *buffer = malloc((size_t)msgsize);
    if (!buffer) {
        perror("malloc logger");
        mq_close(mq_recv);
        return NULL;
    }

    printf("[LOGGER] Iniciado.\n");

    while (g_running) {
        ssize_t n = mq_receive(mq_recv, buffer, (size_t)msgsize, NULL);
        if (n >= 0) {
            printf("[LOG] %s\n", buffer);
            fflush(stdout);
        } else {
            if (errno == EINTR) continue;
            perror("mq_receive");
            break;
        }
    }

    free(buffer);
    mq_close(mq_recv);
    return NULL;
}

/* ------------------------------------------------------------ */
/* Dispatcher: lê do pipe e insere na fila de jobs              */
/* ------------------------------------------------------------ */
typedef struct {
    int pipe_read_fd;
} dispatcher_args_t;

static void *dispatcher_thread(void *arg)
{
    dispatcher_args_t *pargs = (dispatcher_args_t *)arg;
    int fd = pargs->pipe_read_fd;
    sensor_msg_t msg;

    send_log("Dispatcher iniciado (lendo do pipe).");

    while (g_running) {
        ssize_t n = read(fd, &msg, sizeof(msg));
        if (n == 0) {
            /* EOF do pipe → processo sensor terminou */
            send_log("Dispatcher: EOF do pipe, encerrando.");
            break;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            perror("dispatcher read");
            break;
        } else if (n != (ssize_t)sizeof(msg)) {
            /* leitura parcial – para este exemplo, consideramos erro */
            fprintf(stderr, "dispatcher: leitura parcial.\n");
            break;
        }

        job_queue_push(&g_queue, &msg);
    }

    return NULL;
}

/* ------------------------------------------------------------ */
/* Thread trabalhadora: consome jobs da fila, atualiza SHM, log */
/* ------------------------------------------------------------ */
static void *worker_thread(void *arg)
{
    long id = (long)arg;
    char logbuf[128];

    snprintf(logbuf, sizeof(logbuf),
             "Worker %ld iniciado.", id);
    send_log(logbuf);

    while (g_running) {
        sensor_msg_t msg;
        job_queue_pop(&g_queue, &msg);

        /* Processamento simples: se valor > 80, gera alerta    */
        int is_alert = (msg.value > 80);

        /* Atualiza memória compartilhada com semáforo process-shared */
        if (sem_wait(&g_shm->sem) < 0) {
            perror("sem_wait");
            continue;
        }

        g_shm->last_value = msg.value;
        g_shm->last_ts    = msg.timestamp;

        sem_post(&g_shm->sem);

        struct tm *tm_info = localtime(&msg.timestamp);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

        snprintf(logbuf, sizeof(logbuf),
                 "Worker %ld processou valor=%d (ts=%s)%s",
                 id, msg.value, timebuf,
                 is_alert ? " [ALERTA]" : "");
        send_log(logbuf);
    }

    snprintf(logbuf, sizeof(logbuf),
             "Worker %ld encerrando.", id);
    send_log(logbuf);

    return NULL;
}

/* ------------------------------------------------------------ */
/* Tratador simples de SIGINT para encerrar o programa          */
/* ------------------------------------------------------------ */
static void handle_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------ */
/* Função principal                                             */
/* ------------------------------------------------------------ */
int main(void)
{
    int pipefd[2];
    pid_t child_pid;
    pthread_t disp_tid;
    pthread_t log_tid;
    pthread_t worker_tids[NUM_WORKERS];
    dispatcher_args_t disp_args;

    signal(SIGINT, handle_sigint);

    /* 1) Cria pipe para comunicação com processo coletor */
    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    /* 2) Cria processo filho e executa sensor_proc */
    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (child_pid == 0) {
        /* FILHO: redireciona stdout para o lado de escrita do pipe */
        close(pipefd[0]); /* não lê */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(pipefd[1]);

        execl("./sensor_proc", "./sensor_proc", (char *)NULL);
        perror("execl sensor_proc");
        exit(EXIT_FAILURE);
    }

    /* PAI: fecha lado de escrita; ficará só com leitura */
    close(pipefd[1]);

    printf("=== SysMon-IPC – Servidor Principal ===\n");

    /* 3) Configuração da fila de mensagens POSIX */
    struct mq_attr attr;
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = 10;
    attr.mq_msgsize = 128;
    attr.mq_curmsgs = 0;

    mq_unlink(MQ_NAME); /* limpa fila anterior, se existir */

    g_mq_send = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (g_mq_send == (mqd_t)-1) {
        perror("mq_open send");
        exit(EXIT_FAILURE);
    }

    /* 4) Memória compartilhada POSIX */
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, sizeof(shared_data_t)) < 0) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }

    g_shm = mmap(NULL, sizeof(shared_data_t),
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED, shm_fd, 0);
    if (g_shm == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    /* Inicializa semáforo process-shared na SHM (pshared=1) */
    if (sem_init(&g_shm->sem, 1, 1) < 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }
    g_shm->last_value = 0;
    g_shm->last_ts    = time(NULL);

    /* 5) Inicializa fila interna de jobs */
    if (job_queue_init(&g_queue) != 0) {
        fprintf(stderr, "Falha ao inicializar job_queue.\n");
        exit(EXIT_FAILURE);
    }

    /* 6) Cria thread de logger */
    if (pthread_create(&log_tid, NULL, logger_thread, NULL) != 0) {
        perror("pthread_create logger");
        exit(EXIT_FAILURE);
    }

    /* 7) Cria dispatcher (le pipe → insere em fila) */
    disp_args.pipe_read_fd = pipefd[0];
    if (pthread_create(&disp_tid, NULL, dispatcher_thread, &disp_args) != 0) {
        perror("pthread_create dispatcher");
        exit(EXIT_FAILURE);
    }

    /* 8) Cria threads trabalhadoras */
    for (long i = 0; i < NUM_WORKERS; ++i) {
        if (pthread_create(&worker_tids[i], NULL,
                           worker_thread, (void *)i) != 0) {
            perror("pthread_create worker");
            exit(EXIT_FAILURE);
        }
    }

    /* 9) Aguarda SIGINT (Ctrl+C) – aqui só ficamos bloqueados */
    printf("Servidor rodando. Pressione Ctrl+C para encerrar.\n");

    while (g_running) {
        sleep(1);
    }

    printf("\nEncerrando servidor...\n");

    /* 10) Join nas threads */
    pthread_join(disp_tid, NULL);
    for (int i = 0; i < NUM_WORKERS; ++i) {
        pthread_join(worker_tids[i], NULL);
    }
    pthread_join(log_tid, NULL);

    /* 11) Limpeza básica */
    job_queue_destroy(&g_queue);

    mq_close(g_mq_send);
    mq_unlink(MQ_NAME);

    sem_destroy(&g_shm->sem);
    munmap(g_shm, sizeof(shared_data_t));
    shm_unlink(SHM_NAME);

    /* Espera processo filho (sensor_proc) terminar */
    kill(child_pid, SIGTERM);
    waitpid(child_pid, NULL, 0);

    printf("Servidor finalizado.\n");
    return 0;
}
