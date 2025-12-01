# Projeto-SOE
Trabalho prático da disciplina de Sistemas Operacionais Embarcados no curso de Mestrado Profissional em Engenharia Elétrica da UEA.


## 1. **Objetivo do Projeto**

Este projeto foi desenvolvido como atividade prática da disciplina de **Sistemas Operacionais Embarcados** com o objetivo de demonstrar, em uma aplicação única e funcional, o uso integrado de:

- **Programação concorrente com processos e threads;**
- **Mecanismos de sincronização (mutex, semáforos, variáveis de condição);**
- **Mecanismos de comunicação entre processos (IPC)**: pipe, filas de mensagens POSIX e memória compartilhada POSIX.

A aplicação simula um sistema de monitoramento em que leituras de sensores são produzidas em um processo separado, processadas por múltiplas threads em um servidor e disponibilizadas em memória compartilhada para outros processos.

---

## 2. **Arquitetura Geral**

### Visão de alto nível

- `servidor_main`  
  - Cria um **pipe** anônimo.
  - Executa o processo `sensor_proc` via `fork + exec`, redirecionando o `stdout` do filho para o pipe.
  - Cria uma **fila interna de jobs** implementando o padrão **produtor–consumidor**.
  - Cria um conjunto de **threads trabalhadoras** (`pthread_create`) que consomem jobs da fila.
  - Cria uma **fila de mensagens POSIX** para logs (`mq_open`, `mq_send`, `mq_receive`).
  - Cria e inicializa uma **memória compartilhada POSIX** (`shm_open`, `ftruncate`, `mmap`) protegida por **semáforo POSIX process-shared**.
  - Cria uma **thread logger** que recebe mensagens da fila POSIX e imprime na tela.

- `sensor_proc`  
  - Processo filho que gera periodicamente leituras de um sensor fictício (valores inteiros aleatórios).
  - Escreve as medições no pipe herdado do servidor (via `write` em `STDOUT_FILENO`, já redirecionado).

- `visualizador_shm` (opcional)  
  - Processo que mapeia a mesma memória compartilhada.
  - Em loop, usa o semáforo para ler, de forma consistente, o último valor de sensor processado e exibe na tela.

---

## 3. **Mecanismos Utilizados**

### Programação concorrente

- **Processos**: `fork`, `exec`, `waitpid`, `exit`
- **Threads POSIX**: `pthread_create`, `pthread_join`
- **Modelo produtor–consumidor** com fila circular de jobs (`job_queue.c/.h`)

### Sincronização

- **Mutexes** (`pthread_mutex_t`): exclusão mútua no acesso à fila de jobs.
- **Variáveis de condição** (`pthread_cond_t`): bloqueio e desbloqueio de produtores e consumidores (`not_empty` / `not_full`).
- **Semáforo POSIX process-shared** (`sem_t` em `shared_data_t`): coordena o acesso à área de memória compartilhada entre processos distintos.
- **Seção crítica**: atualização do último valor em memória compartilhada e manipulação da fila interna.

### Comunicação entre Processos (IPC)

- **Pipe anônimo**: `sensor_proc → servidor_main` (envio de estruturas `sensor_msg_t`).
- **Fila de mensagens POSIX**:
  - `mq_open`, `mq_send` em `servidor_main` (threads trabalhadoras);
  - `mq_receive` na `logger_thread`.
- **Memória compartilhada POSIX**:
  - `shm_open`, `ftruncate`, `mmap` criam/mapem a estrutura `shared_data_t`;
  - usada para expor o último valor de sensor para qualquer processo que mapeie o mesmo segmento (`visualizador_shm`).

---

## 4. **Organização do Código**

```text
Projeto-SOE/
  ├─ include/
  │   ├─ common.h        # tipos e constantes globais (sensor_msg_t, nomes de recursos IPC)
  │   ├─ shared_data.h   # estrutura armazenada na memória compartilhada (sem_t + dados)
  │   └─ job_queue.h     # fila de jobs (produtor–consumidor)
  ├─ src/
  │   ├─ servidor_main.c     # processo principal (pipe, threads, MQ, SHM, semáforos)
  │   ├─ sensor_proc.c       # processo coletor de sensores
  │   ├─ visualizador_shm.c  # processo leitor da memória compartilhada (opcional)
  │   └─ job_queue.c         # implementação da fila de jobs
  └─ README.md
```

## 5. **Como compilar**

#### Servidor:

```bash
gcc -Wall -Wextra -O2 -Iinclude src/servidor_main.c src/job_queue.c -o servidor_main -lpthread
```

#### Processo do sensor:

```bash
gcc -Wall -Wextra -O2 -Iinclude src/sensor_proc.c -o sensor_proc -lpthread
```

#### Visualizador:

```bash
gcc -Wall -Wextra -O2 -Iinclude src/visualizador_shmem.c -o visualizador_shmem -lpthread
```

---

## 6. **Como Executar**

Abra **3 terminais**:

---

### **1️⃣ Terminal 1 — Executar o servidor**

```bash
./servidor_main
```

Ele:

* Cria MQ e SHM
* Cria threads consumidoras
* Dispara o processo sensor via `fork + exec`

---

### **2️⃣ Terminal 2 — Executar o visualizador**

```bash
./visualizador_shmem
```

Ele exibe continuamente os valores atualizados na memória compartilhada.

---

### **3️⃣ Terminal 3 — (Opcional) Rodar sensores manualmente**

```bash
./sensor_proc
```

---

