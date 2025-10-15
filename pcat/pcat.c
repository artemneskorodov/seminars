#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

#define BUFFER_SIZE (4)

int
copy_file(int fd_from,
          int fd_to)
{
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
main(int         argc,
     const char *argv[])
{
    int pipefds[2];
    if (pipe(pipefds) != 0)
    {
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        // Child
        close(pipefds[0]);
        if (argc == 1)
        {
            if (copy_file(STDIN_FILENO, pipefds[1]) != EXIT_SUCCESS)
            {
                close(pipefds[1]);
                return EXIT_FAILURE;
            }
            close(pipefds[1]);
            return EXIT_SUCCESS;
        }
        for (int i = 1; i < argc; ++i)
        {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0)
            {
                perror(argv[i]);
                continue;
            }

            if (copy_file(fd, pipefds[1]) != EXIT_SUCCESS)
            {
                close(fd);
                close(pipefds[1]);
                return EXIT_FAILURE;
            }
            close(fd);
        }
        close(pipefds[1]);
        return EXIT_SUCCESS;
    }
    // Parent
    close(pipefds[1]);
    if (copy_file(pipefds[0], STDOUT_FILENO) != EXIT_SUCCESS)
    {
        close(pipefds[0]);
        return EXIT_FAILURE;
    }
    close(pipefds[0]);

    waitpid(pid, NULL, 0);
    return EXIT_SUCCESS;
}
