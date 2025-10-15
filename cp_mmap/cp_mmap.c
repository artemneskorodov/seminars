#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int
main(int         argc,
     const char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "%s: usage: %s [from] [to]\n");
        return EXIT_FAILURE;
    }

    // Size of initial file
    struct stat st;
    if (stat(argv[1], &st) != 0)
    {
        fprintf(stderr, "STAT(to): ");
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    // Opening initial file
    int fd_from = open(argv[1], O_RDONLY);
    if (fd_from < 0)
    {
        fprintf(stderr, "OPEN(from): ");
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    // Openinig destination file
    int fd_to = open(argv[2], O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd_to < 0)
    {
        close(fd_from);

        fprintf(stderr, "OPEN(to): ");
        perror(argv[2]);
        return EXIT_FAILURE;
    }

    // Truncating destination to size of initial file
    if (ftruncate(fd_to, st.st_size) != 0)
    {
        close(fd_from);
        close(fd_to);

        fprintf(stderr, "FTRUNCATE(to): ");
        perror(argv[2]);
        return EXIT_FAILURE;
    }

    void *mmaped_from = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd_from, 0);
    if (mmaped_from == MAP_FAILED)
    {
        close(fd_from);
        close(fd_to);

        fprintf(stderr, "MMAP(from): ");
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    void *mmaped_to = mmap(NULL, st.st_size, PROT_WRITE, MAP_SHARED, fd_to, 0);
    if (mmaped_to == MAP_FAILED)
    {
        close(fd_from);
        close(fd_to);
        munmap(mmaped_from, st.st_size);

        fprintf(stderr, "MMAP(to): ");
        perror(argv[2]);
        return EXIT_FAILURE;
    }

    memcpy(mmaped_to, mmaped_from, st.st_size);

    munmap(mmaped_from, st.st_size);
    munmap(mmaped_to, st.st_size);
    close(fd_from);
    close(fd_to);

    return EXIT_SUCCESS;
}
