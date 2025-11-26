#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdbool.h>

#define BUFFER_SIZE (4)

typedef struct
{
    size_t bytes;
    size_t words;
    size_t lines;
} mywc_info_t;

int
copy_file(int          fd_from,
          int          fd_to,
          mywc_info_t *info)
{
    bool in_word = false;
    char buffer[BUFFER_SIZE];
    while (true)
    {
        ssize_t read_bytes = read(fd_from, buffer, BUFFER_SIZE);
        if (read_bytes < 0)
        {
            perror("Reading error");
            return EXIT_FAILURE;
        } else if (read_bytes == 0)
        {
            break;
        }

        info->bytes += read_bytes;
        for (ssize_t i = 0; i < read_bytes; ++i)
        {
            if (buffer[i] == '\n')
            {
                info->lines++;
            }
            if (!isspace(buffer[i]))
            {
                if (!in_word)
                {
                    info->words++;
                    in_word = true;
                }
            } else
            {
                in_word = false;
            }
        }

        // TODO determine words

        ssize_t written = 0;
        while (written < read_bytes)
        {
            ssize_t write_bytes = write(fd_to, buffer + written, read_bytes - written);
            if (write_bytes < 0)
            {
                perror("Writing error");
                return EXIT_FAILURE;
            }
            written += write_bytes;
        }
    }
    return EXIT_SUCCESS;
}

int
main(int          argc,
     char* const* argv) {
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s [proc]", argv[0]);
        return EXIT_FAILURE;
    }

    int pipefds[2];
    if (pipe(pipefds) != 0)
    {
        perror("pipe");
        return EXIT_FAILURE;
    }

    // Start time
    struct timeval start;
    if (gettimeofday(&start, NULL) != 0) {
        perror("gettimeofday");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Closing read ds for child
        close(pipefds[0]);
        // Dupping write fd to stdout (1)
        dup2(pipefds[1], STDOUT_FILENO);
        // Closing write fd
        close(pipefds[1]);
        // Runtting program in child proccess
        execvp(argv[1], argv + 1);
        // Failure if was here
        perror(argv[1]);
        return EXIT_FAILURE;
    }
    // Closing write fd for parent
    close(pipefds[1]);

    // Copying pipe to stdout and counting info
    mywc_info_t info = {0};
    copy_file(pipefds[0], STDOUT_FILENO, &info);

    // Waiting for child
    if (wait(NULL) != pid) {
        perror("waitpid");
        return EXIT_FAILURE;
    }

    // End time
    struct timeval end;
    if (gettimeofday(&end, NULL) != 0)
    {
        perror("gettimeofday");
        return EXIT_FAILURE;
    }
    double msec_end = (double)end.tv_sec * 1000. + (double)end.tv_usec / 1000.;
    double msec_start = (double)start.tv_sec * 1000. + (double)start.tv_usec / 1000.;

    printf("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\n"
           "\t-time:  %lg ms\n"
           "\t-bytes: %lu\n"
           "\t-words: %lu\n"
           "\t-lines: %lu\n"
           "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\n",
           msec_end - msec_start,
           info.bytes,
           info.words,
           info.lines);
    return EXIT_SUCCESS;
}
