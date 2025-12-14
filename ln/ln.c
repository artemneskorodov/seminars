#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>

int create_link( const char *value, const char *path);

int read_link( const char *path);

int
usage( const char *prog)
{
    fprintf( stderr, "Usage: (%s -s <from> <to>) or (%s -r <link>)\n", prog, prog);
    return EXIT_FAILURE;
}


typedef struct
{
    bool flag_symbolic;
    bool flag_read;
} ln_mode_t;

int
main( int    argc,
      char **argv)
{
    if ( argc < 2 )
    {
        return usage( argv[0]);
    }
    ln_mode_t mode = {};
    if ( strcmp( argv[1], "-s") == 0 ||
         strcmp( argv[1], "--symbolic") == 0)
    {
        mode.flag_symbolic = true;
    } else if ( strcmp( argv[1], "-r") == 0 ||
                strcmp( argv[1], "--read") == 0 )
    {
        mode.flag_read = true;
    } else
    {
        return usage( argv[0]);
    }

    if ( mode.flag_symbolic )
    {
        if ( argc != 4 )
        {
            return usage( argv[0]);
        }
        return create_link( argv[2], argv[3]);
    } else if ( mode.flag_read )
    {
        if ( argc != 3 )
        {
            return usage( argv[0]);
        }
        return read_link( argv[2]);
    }
}

int
create_link( const char *value,
             const char *path)
{
    if ( symlink( value, path) != 0 )
    {
        perror( path);
        return errno;
    }
    return 0;
}

int
read_link( const char *path)
{
    struct stat st;
    if ( lstat( path, &st) != 0 )
    {
        perror( path);
        return errno;
    }

    char *buffer = (char *)calloc( st.st_size + 1, sizeof( char));
    if ( buffer == NULL )
    {
        perror( "Memory allocation failed");
        return EXIT_FAILURE;
    }

    ssize_t result = readlink( path, buffer, st.st_size + 1);
    if ( result < 0 )
    {
        free( buffer);
        perror( path);
        return errno;
    } else if ( result == st.st_size + 1 )
    {
        free( buffer);
        fprintf( stderr, "Size increased since lstat.\n");
        return EXIT_FAILURE;
    }

    printf( "%s\n", buffer);

    free( buffer);
    return 0;
}
