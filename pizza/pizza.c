#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

// #define LOG_OFF

#ifndef LOG_OFF
    #define DUMP() dump_table(shared)
#else
    #define DUMP(...)
#endif

static const char *kPizzaString         = "pizza";
static const char *kSharedMemoryObjName = "/pizza";
static const char *kReadySemName        = "/pizza_ready";
static const char *kEmptySemName        = "/pizza_empty";
static const char *kTableSemName        = "/pizza_table";

#define MAX_PIZZA_COUNT                   (5)
#define PIZZA_SIZE                        (sizeof("pizza") - 1)
#define MAX_CHIEF_WORK                    (10)

#define RESET                             "\033[0m"
#define BLUE                              "\033[1;34m"
#define GREEN                             "\033[1;32m"
#define RED                               "\033[1;31m"

typedef struct
{
    char buffer[MAX_PIZZA_COUNT * PIZZA_SIZE];
    int  head;
    int  tail;
    int  chiefs_count;
    int  pizza_count;
} shared_table_t;

static int  run_chief   (shared_table_t *shared, sem_t *table, sem_t *ready, sem_t *empty, int num);
static int  run_courier (shared_table_t *shared, sem_t *table, sem_t *ready, sem_t *empty, int num);
static void dump_table  (shared_table_t *shared);

int
main(int         argc,
     const char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "%s: usage: %s [n_chiefs] [n_couriers]\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    int n_chiefs   = atoi(argv[1]);
    int n_couriers = atoi(argv[2]);

    int shmem_fd = shm_open(kSharedMemoryObjName, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (shmem_fd < 0)
    {
        fprintf(stderr, "shm_open for shared memory failed: ");
        perror(kSharedMemoryObjName);
        return EXIT_FAILURE;
    }

    if (ftruncate(shmem_fd, sizeof(shared_table_t)) != 0)
    {
        shm_unlink(kSharedMemoryObjName);

        fprintf(stderr, "ftruncate shared memory failed: ");
        perror(kSharedMemoryObjName);
        return EXIT_FAILURE;
    }
    shared_table_t *pizzas = (shared_table_t *)mmap(NULL, sizeof(shared_table_t), PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0);
    if (pizzas == MAP_FAILED)
    {
        shm_unlink(kSharedMemoryObjName);

        fprintf(stderr, "Map failed: ");
        perror(kSharedMemoryObjName);
        return EXIT_FAILURE;
    }
    shm_unlink(kSharedMemoryObjName);

    sem_t *ready = sem_open(kReadySemName, O_CREAT | O_EXCL, 0666, 0);
    if (ready == SEM_FAILED)
    {
        munmap(pizzas, sizeof(shared_table_t));

        perror(kReadySemName);
        return EXIT_FAILURE;
    }
    sem_t *empty = sem_open(kEmptySemName, O_CREAT | O_EXCL, 0666, MAX_PIZZA_COUNT);
    if (empty == SEM_FAILED)
    {
        munmap(pizzas, sizeof(shared_table_t));
        sem_close(ready);

        perror(kEmptySemName);
        return EXIT_FAILURE;
    }
    sem_t *table = sem_open(kTableSemName, O_CREAT | O_EXCL, 0666, 1);
    if (table == SEM_FAILED)
    {
        munmap(pizzas, sizeof(shared_table_t));
        sem_close(ready);
        sem_close(empty);

        perror(kTableSemName);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < n_chiefs; ++i)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            return run_chief(pizzas, table, ready, empty, i);
        }
    }
    for (int i = 0; i < n_couriers; ++i)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            return run_courier(pizzas, table, ready, empty, i);
        }
    }

    printf("Waiting.\n");
    for (int i = 0; i < n_chiefs + n_couriers; ++i)
    {
        wait(NULL);
    }
    dump_table(pizzas);
    sem_unlink(kReadySemName);
    sem_unlink(kEmptySemName);
    sem_unlink(kTableSemName);
    munmap(pizzas, sizeof(shared_table_t));
    return EXIT_SUCCESS;
}

#ifndef LOG_OFF
    #define LOG(_format, ...) printf(BLUE "  Chief[%d]: " RESET _format ".\n", num , ##__VA_ARGS__)
#else
    #define LOG(...)
#endif

static int
run_chief(shared_table_t *shared,
          sem_t          *table,
          sem_t          *ready,
          sem_t          *empty,
          int             num)
{
    sem_wait(table);
    shared->chiefs_count++;
    sem_post(table);
    LOG("Is here");

    for (int i = 0; i < MAX_CHIEF_WORK; ++i)
    {
        LOG("Waiting to get free space for pizza");
        sem_wait(empty);
        sem_wait(table);

        memcpy(&shared->buffer[shared->head * PIZZA_SIZE], kPizzaString, PIZZA_SIZE);
        shared->head = (shared->head + 1) % MAX_PIZZA_COUNT;
        shared->pizza_count++;

        LOG("Now pizza looks like this");
        DUMP();

        LOG("Freeing table");
        sem_post(table);
        LOG("Adding one to ready pizzas");
        sem_post(ready);
    }
    sem_wait(table);
    shared->chiefs_count--;
    sem_post(table);

    LOG("Finished");
    return EXIT_SUCCESS;
}

#undef LOG

#ifndef LOG_OFF
    #define LOG(_format, ...) printf(GREEN "Courier[%d]: " RESET _format ".\n", num , ##__VA_ARGS__)
#else
    #define LOG(...)
#endif

static int
run_courier(shared_table_t *shared,
            sem_t          *table,
            sem_t          *ready,
            sem_t          *empty,
            int             num)
{
    LOG("Is here");
    while (shared->chiefs_count != 0 || shared->pizza_count != 0)
    {
        if (sem_trywait(ready) != 0 && errno == EAGAIN)
        {
            continue;
        }
        if(sem_trywait(table) != 0 && errno == EAGAIN)
        {
            sem_post(ready);
            continue;
        }
        LOG("Got one pizza");

        char *buffer = &shared->buffer[shared->tail * PIZZA_SIZE];

        if (memcmp(buffer, kPizzaString, PIZZA_SIZE) != 0)
        {
            LOG("Bad pizza on position %d", shared->tail);
            DUMP();

            // Restoring semaphores
            sem_post(table);
            sem_post(ready);
            return EXIT_FAILURE;
        }
        memset(buffer, 0, PIZZA_SIZE);
        shared->tail = (shared->tail + 1) % MAX_PIZZA_COUNT;
        shared->pizza_count--;

        LOG("Now table looks like this");
        DUMP();

        LOG("Freeing table");
        sem_post(table);
        LOG("Adding empty places");
        sem_post(empty);
    }
    LOG("Finished");
    return EXIT_SUCCESS;
}

#undef LOG

static void
dump_table(shared_table_t *shared)
{
    printf(RED);
    putchar('\t');
    putchar('|');
    for (size_t i = 0; i < MAX_PIZZA_COUNT; ++i)
    {
        for (size_t j = 0; j < PIZZA_SIZE; ++j)
        {
            char data = shared->buffer[i * PIZZA_SIZE + j];
            if (data == 0)
            {
                putchar('-');
            } else
            {
                putchar(data);
            }
        }
        putchar('|');
    }
    printf(RESET);
    putchar('\n');
}
