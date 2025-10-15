#include <stdio.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/time.h>

static int judge(int runners_cnt, int msg_id);
static int runner(int runners_cnt, int curr, int msg_id);

typedef enum
{
    STADIUM_STATUS_RUNNER_CREATED   = 1,
    STADIUM_STATUS_NEXT_PARTICIPANT = 2,
    STADIUM_STATUS_ALL_CREATED      = 3,
} stadium_status_t;

typedef struct
{
    long mtype;
    stadium_status_t status;
} msg_t;

int
main(int         argc,
     const char *argv[])
{
    // Parsing parameters
    if (argc != 2)
    {
        fprintf(stderr, "Expected parameter N (number of runners)\n");
        return EXIT_FAILURE;
    }
    int runners_cnt = atoi(argv[1]);

    // Creating msg queue
    int msg_id = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    if (msg_id < 0)
    {
        perror("Message queue error");
        return EXIT_FAILURE;
    }

    // Creating judge
    pid_t judge_pid = fork();
    if (judge_pid == 0)
    {
        // Judge child proccess
        return judge(runners_cnt, msg_id);
    }

    // Creating runners
    for (int i = 1; i <= runners_cnt; ++i)
    {
        pid_t runner_pid = fork();
        if (runner_pid == 0)
        {
            // Runner child proccess
            return runner(runners_cnt, i, msg_id);
        }
    }

    for (int i = 0; i < runners_cnt + 1; ++i)
    {
        wait(NULL);
    }

    if (msgctl(msg_id, IPC_RMID, 0) == -1)
    {
        perror("Message queue destructing error");
        return EXIT_FAILURE;
    }
}

static int
judge(int runners_cnt,
      int msg_id)
{
    msg_t msg;
    printf("Judge:      Created\n");

    // Waiting for all runners
    for (int i = 1; i <= runners_cnt; ++i)
    {
        if (msgrcv(msg_id, &msg, sizeof(msg), 0, 0) == -1)
        {
            perror("Rcv failed in judge");
            return EXIT_FAILURE;
        }
        printf("Judge:      Saw runnner %d\n", msg.mtype);
    }
    printf("Judge:      All runners are on stadium!\n");

    // Sending info that all runners are on stadium:
    msg.mtype = 1;
    msg.status = STADIUM_STATUS_ALL_CREATED;
    if (msgsnd(msg_id, &msg, sizeof(msg), 0) == -1)
    {
        perror("Snd failed in judge");
        return EXIT_FAILURE;
    }

    // Start time
    struct timeval start;
    if (gettimeofday(&start, NULL) != 0)
    {
        perror("gettimeofday");
        return EXIT_FAILURE;
    }

    // Sending start to first
    msg.mtype = (runners_cnt + 1) + 2;
    msg.status = STADIUM_STATUS_NEXT_PARTICIPANT;
    if (msgsnd(msg_id, &msg, sizeof(msg), 0) == -1)
    {
        perror("Snd failed in judge");
        return EXIT_FAILURE;
    }

    // Getting end of last
    if (msgrcv(msg_id, &msg, sizeof(msg), (runners_cnt + 1) + runners_cnt + 2, 0) == -1)
    {
        perror("Rcv failed in judge");
        return EXIT_FAILURE;
    }

    // End time
    struct timeval end;
    if (gettimeofday(&end, NULL) != 0)
    {
        perror("gettimeofday");
        return EXIT_FAILURE;
    }
    double msec_end   = (double)end.tv_sec   * 1000. + (double)end.tv_usec   / 1000.;
    double msec_start = (double)start.tv_sec * 1000. + (double)start.tv_usec / 1000.;
    printf("Judge:      Finished in %lg ms!!!\n", msec_end - msec_start);

    return EXIT_SUCCESS;
}

static int
runner(int runners_cnt,
       int curr,
       int msg_id)
{
    msg_t msg = {.mtype = curr + 1, .status = STADIUM_STATUS_RUNNER_CREATED};

    printf("Runner % 3d: Sending hello msg to judge\n", curr);
    if (msgsnd(msg_id, &msg, sizeof(msg), 0) == -1)
    {
        perror("Rcv failed for runner");
        return EXIT_FAILURE;
    }

    // Waiting for start
    if (msgrcv(msg_id, &msg, sizeof(msg), (runners_cnt + 1) + curr + 1, 0) == -1)
    {
        perror("Rcv failed for runner");
        return EXIT_FAILURE;
    }
    if (msg.status != STADIUM_STATUS_NEXT_PARTICIPANT)
    {
        fprintf(stderr, "Unexpected status received in runner %d: msg.status = %d\n", curr, msg.status);
        return EXIT_FAILURE;
    }

    // --- --- --- ---
    printf("Runner % 3d: Started his run\n", curr);
    // Run
    printf("Runner % 3d: Finished his run\n", curr);
    // --- --- --- ---

    // Sending msg to next
    msg.mtype = (runners_cnt + 1) + curr + 2;
    msg.status = STADIUM_STATUS_NEXT_PARTICIPANT;
    if (msgsnd(msg_id, &msg, sizeof(stadium_status_t), 0) == -1)
    {
        perror("Snd failed for runner");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
