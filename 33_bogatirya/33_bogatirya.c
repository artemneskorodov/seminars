#include <iostream>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>

struct msg_t
{
    msg_t ( long mtype_, int from_, int value_) : mtype{ mtype_}, from{ from_}, value{ value_} {}
    msg_t () : mtype{ 0}, from{ 0}, value{ 0} {}

    long       mtype; // kMainMtype to send broadcast to everyone
    int        from;  // Set by bogatir, to be identified by main, which is needed to create broadcast messages
    int        value;
};

const size_t kMsgSize     = sizeof( msg_t) - sizeof( msg_t::mtype);
const long   kMainMtype   = 1;
const int    kFirstSymbol = ' '; // Fist printing symbol
const int    kLastSymbol  = '~'; // Last printing symbol in 7 bit ascii
const int    kBogatirsNum = kLastSymbol - kFirstSymbol + 1;

inline long gen_mtype_from_main( int id) { return 3 + id; }

int
bogatir( int         msg_id,
         int         id,     // Used only to be identified by main functions, which creates broadcast
         const char *string)
{
    int myval = 0;
    int values[kBogatirsNum] = {};

    for ( ; ; )
    {
        // Generate my random value and send it to everybody
        srand( time( nullptr) ^ getpid());
        do
        {
            myval = rand();
        } while ( myval == 0 ); // Skip zeros

        msg_t msg{ kMainMtype, id, myval};
        if ( msgsnd( msg_id, &msg, kMsgSize, 0) < 0 )
        {
            perror( "msgsnd");
            return -errno;
        }

        // Get all values
        values[0] = myval;
        for ( int i = 1; i != kBogatirsNum; ++i )
        {
            if ( msgrcv( msg_id, &msg, kMsgSize, gen_mtype_from_main( id), 0) < 0 )
            {
                perror( "msgrcv");
                return -errno;
            }
            values[i] = msg.value;
        }

        // Sort
        std::sort( &values[0], &values[kBogatirsNum - 1]);

        // Check if unique
        bool is_unique = true;
        for ( int i = 0; i != kBogatirsNum; ++i )
        {
            if ( values[i] == values[i + 1] )
            {
                is_unique = false;
                break;
            }
        }
        if ( is_unique )
        {
            break;
        }
    }

    // Only if all proccesses have unique values we can get here
    int my_id = -1;
    for ( int i = 0; i != kBogatirsNum; ++i )
    {
        if ( values[i] == myval )
        {
            my_id = i;
            break;
        }
    }

    int symbol = kFirstSymbol + my_id;
    if ( my_id == 0 )
    {
        for ( const char *pos = string; *pos != '\0'; ++pos )
        {
            if ( *pos == symbol )
            {
                std::putchar( symbol);
                std::fflush( stdout);
            } else
            {
                // Command to symbol owner
                msg_t msg{ kMainMtype, id, *pos};
                if ( msgsnd( msg_id, &msg, kMsgSize, 0) < 0 )
                {
                    perror( "msgsnd");
                    return -errno;
                }
                if ( msgrcv( msg_id, &msg, kMsgSize, gen_mtype_from_main( id), 0) < 0 )
                {
                    perror( "msgrcv");
                    return -errno;
                }
            }
        }
        std::putchar( '\n');
        std::fflush( stdout);
        msg_t msg{ kMainMtype, id, 0};
        if ( msgsnd( msg_id, &msg, kMsgSize, 0) < 0 )
        {
            std::perror( "msgsnd");
            return -errno;
        }
        return EXIT_SUCCESS;
    } else
    {
        for ( ; ; )
        {
            msg_t msg{};
            if ( msgrcv( msg_id, &msg, kMsgSize, gen_mtype_from_main( id), 0) < 0 )
            {
                perror( "msgrcv");
                return -errno;
            }

            if ( msg.value == 0 )
            {
                return EXIT_SUCCESS;
            }

            if ( msg.value != symbol )
            {
                continue;
            }

            std::putchar( symbol);
            std::fflush( stdout);

            msg = msg_t{ kMainMtype, id, symbol};
            if ( msgsnd( msg_id, &msg, kMsgSize, 0) < 0 )
            {
                perror( "msgsnd");
                return -errno;
            }
        }
    }
}

int
main( int         argc,
      const char *argv[])
{
    if ( argc != 2 )
    {
        std::cerr << "Usage: " << argv[0] << " <string to print>" << std::endl;
        return EXIT_FAILURE;
    }

    int msg_id = msgget( IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    if ( msg_id < 0 )
    {
        perror( "Message queue error");
        return EXIT_FAILURE;
    }

    for ( int i = 0; i != kBogatirsNum; ++i )
    {
        pid_t pid = fork();
        if ( pid == 0 )
        {
            return bogatir( msg_id, i, argv[1]);
        }
    }

    for ( ; ; )
    {
        msg_t msg;
        if ( msgrcv( msg_id, &msg, kMsgSize, kMainMtype, 0) < 0 )
        {
            perror( "msgrcv failed");
            return EXIT_FAILURE;
        }
        int from = msg.from;
        msg.from = 0;
        // std::cout << "From " << from << " " << msg.value << std::endl;
        for ( int i = 0; i != kBogatirsNum; ++i )
        {
            if ( i == from )
            {
                continue;
            }
            msg.mtype = gen_mtype_from_main( i);
            msgsnd( msg_id, &msg, kMsgSize, 0);
        }
        if ( msg.value == 0 )
        {
            break;
        }
    }

    for ( int i = 0; i != kBogatirsNum; ++i )
    {
        wait( NULL);
    }

    msgctl( msg_id, IPC_RMID, 0);

    return EXIT_SUCCESS;
}
