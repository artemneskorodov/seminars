#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/select.h>
#include <stdarg.h>

#define BUFFER_SIZE (4096)

typedef struct
{
    size_t bytes;
    size_t words;
    size_t lines;
    bool   in_word;
} mywc_info_t;

void
count_info(char        *buffer,
           size_t       readsz,
           mywc_info_t *info)
{
    info->bytes += readsz;
    for ( ssize_t i = 0; i < readsz; ++i )
    {
        if ( buffer[i] == '\n' )
        {
            info->lines++;
        }
        if ( !isspace(buffer[i]) )
        {
            if ( !info->in_word )
            {
                info->words++;
                info->in_word = true;
            }
        } else
        {
            info->in_word = false;
        }
    }
}

int
copy_files(int          count,    // Number of files to read
           int         *fds_from, // array of descriptors to read from
           int         *fds_to,   // array of descriptors to write to
           mywc_info_t *info)     // array of structures to place info
{
    int fds_max = -1;
    for ( int i = 0; i != count; ++i )
    {
        if ( fds_from[i] > fds_max )
        {
            fds_max = fds_from[i];
        }
    }

    fd_set readfds;
    char buffer[BUFFER_SIZE];
    int nfiles = count;

    while ( nfiles != 0 )
    {
        FD_ZERO( &readfds);
        for ( int i = 0; i != count; ++i )
        {
            FD_SET( fds_from[i], &readfds);
        }

        int ready = select( fds_max + 1, &readfds, NULL, NULL, NULL);
        if ( ready == -1 )
        {
            perror( "Select");
            return EXIT_FAILURE;
        }

        for ( int i = 0; i != count; ++i )
        {
            if ( FD_ISSET( fds_from[i], &readfds) )
            {
                ssize_t read_bytes = read( fds_from[i], buffer, BUFFER_SIZE);
                if ( read_bytes <= 0 )
                {
                    if ( read_bytes < 0 )
                    {
                        perror( "Reading error");
                    }
                    nfiles--;
                    close(fds_from[i]);
                    fds_from[i] = -1;
                    continue;
                }

                count_info( buffer, read_bytes, &info[i]);

                // ssize_t written = 0;
                // while ( written < read_bytes )
                // {
                //     ssize_t write_bytes = write( fds_to[i], buffer + written, read_bytes - written);
                //     if ( write_bytes < 0 )
                //     {
                //         perror( "Writing error");
                //         return EXIT_FAILURE;
                //     }
                //     written += write_bytes;
                // }
            }
        }
    }
    return EXIT_SUCCESS;
}

int
main(int          argc,
     char* const* argv) {
    if ( argc < 2 )
    {
        fprintf( stderr, "usage: %s [proc]", argv[0] );
        return EXIT_FAILURE;
    }

    int pipefds_stdout[2];
    if ( pipe(pipefds_stdout) != 0 )
    {
        perror("pipefds_stdout");
        return EXIT_FAILURE;
    }

    int pipefds_stderr[2];
    if ( pipe(pipefds_stderr) != 0 )
    {
        perror( "pipefds_stderr" );
        return EXIT_FAILURE;
    }

    // Start time
    struct timeval start;
    if ( gettimeofday(&start, NULL) != 0 ) {
        perror( "gettimeofday" );
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Closing read ds for child
        close( pipefds_stdout[0] );
        close( pipefds_stderr[0] );
        // Dupping write fd to stdout (1)
        dup2( pipefds_stdout[1], STDOUT_FILENO );
        dup2( pipefds_stderr[1], STDERR_FILENO );

        close(pipefds_stdout[1]);
        close(pipefds_stderr[1]);
        // Runtting program in child proccess
        execvp( argv[1], argv + 1 );
        // Failure if was here
        return EXIT_FAILURE;
    }
    // Closing write fd for parent
    close( pipefds_stdout[1] );
    close( pipefds_stderr[1] );

    // Copying pipe to stdout and counting info
    int fds_from[2]     = { pipefds_stdout[0], pipefds_stderr[0] };
    int fds_to[2]       = { STDOUT_FILENO, STDERR_FILENO };
    mywc_info_t info[2] = { {}, {} };


    copy_files( 2, fds_from, fds_to, info);

    // Waiting for child
    if ( wait(NULL) != pid ) {
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
    double msec_end   = (double)end.tv_sec   * 1000. + (double)end.tv_usec   / 1000.;
    double msec_start = (double)start.tv_sec * 1000. + (double)start.tv_usec / 1000.;

    printf("╔═══════════════════════════════════════╗\n"
           "║ Time  %lg ms                          ║\n"
           "╟───────────────────────────────────────╢\n"
           "║\tSTDERR:                              ║\n"
           "║\t\t-bytes: % 10lu                     ║\n"
           "║\t\t-words: % 10lu                     ║\n"
           "║\t\t-lines: % 10lu                     ║\n"
           "╟───────────────────────────────────────╢\n"
           "║\tSTDERR:                              ║\n"
           "║\t\t-bytes: % 10lu                     ║\n"
           "║\t\t-words: % 10lu                     ║\n"
           "║\t\t-lines: % 10lu                     ║\n"
           "╚═══════════════════════════════════════╝\n",
           msec_end - msec_start,
           info[0].bytes,
           info[0].words,
           info[0].lines,
           info[1].bytes,
           info[1].words,
           info[1].lines);
    return EXIT_SUCCESS;
}
