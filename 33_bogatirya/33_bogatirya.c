#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_CHILDREN     (128)

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    const char *string;
    size_t string_pos;
    int symbol;
} positions_t;

void *bogatir( void *shared_ptr);

int
main( int         argc,
      const char *argv[]) {

    if ( argc != 2 )
    {
        fprintf(stderr, "%s: usage: %s \"string to print\"\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    positions_t shared;
    shared.symbol      = 1;
    shared.string_pos  = 0;
    shared.string      = argv[1];
    pthread_mutex_init( &shared.mutex, NULL);
    pthread_cond_init( &shared.cond, NULL);

    pthread_t threads[MAX_CHILDREN];

    for ( size_t i = 0; i != MAX_CHILDREN; ++i )
    {
        if ( pthread_create( &threads[i], NULL, bogatir, &shared) != 0 )
        {
            // TODO handle error: stop all bogatirs
            return EXIT_FAILURE;
        }
    }

    for ( size_t i = 0; i != MAX_CHILDREN; ++i )
    {
        pthread_join( threads[i], NULL);
    }
    pthread_mutex_destroy( &shared.mutex);
    pthread_cond_destroy( &shared.cond);

    putchar( '\n');

    return EXIT_SUCCESS;
}

void *
bogatir( void *shared_ptr)
{
    positions_t *shared = (positions_t *)shared_ptr;

    // Getting char to print
    pthread_mutex_lock( &shared->mutex);
    int printing_symbol = shared->symbol;
    shared->symbol++;
    // Wake up all threads if in last thread
    if ( printing_symbol == MAX_CHILDREN )
    {
        pthread_cond_broadcast( &shared->cond);
    }
    pthread_mutex_unlock( &shared->mutex);

    // Printing letters
    for ( ; ; )
    {
        // Waiting for our symbol
        pthread_mutex_lock( &shared->mutex);
        if ( shared->string[shared->string_pos] == '\0' )
        {
            pthread_cond_broadcast( &shared->cond);
            pthread_mutex_unlock( &shared->mutex);
            return NULL;
        } else if ( shared->string[shared->string_pos] != printing_symbol )
        {
            // Waiting while other letters are printing
            pthread_cond_wait( &shared->cond, &shared->mutex);
        } else
        {
            // Printing this threads letter
            putchar( printing_symbol);
            shared->string_pos++;
            // Wake up other threads
            pthread_cond_broadcast( &shared->cond);
        }

        pthread_mutex_unlock( &shared->mutex);
    }
}
