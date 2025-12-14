#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <string>
#include <sstream>
#include <cstring>

pid_t shm_and_sem_id = -1;

void join_request_handler( int signum, siginfo_t *siginfo, void *);
void join_approve_handler( int signum, siginfo_t *siginfo, void *);
void msg_handler         ( int signum, siginfo_t *siginfo, void *);

namespace tg
{

enum status_t
{
    STATUS_SUCCESS           = 0,
    STATUS_EXIT              = 1,
    STATUS_SHARED_ERROR      = 2,
    STATUS_USAGE_ERROR       = 3,
    STATUS_INPUT_ERROR       = 4,
    STATUS_SIGNAL_ERROR      = 5,
    STATUS_NO_SPACE_FOR_USER = 6,
    STATUS_NO_SPACE_FOR_MSG  = 7,
    STATUS_STRING_OVERFLOW   = 8,
};

const int kSignalJoinRequest  = SIGRTMIN + 0;
const int kSignalJoinApproved = SIGRTMIN + 1;
const int kSignalMsg          = SIGRTMIN + 2;

const size_t kMaxUsers  = 10;
const size_t kMaxMsgLen = 256;
const size_t kMaxMsgNum = 256;

struct msg_t
{
    char   data[kMaxMsgLen + 1];
    size_t length;
};

class chat_t
{
public:
    bool Valid() { return valid_; }

    status_t
    Init()
    {
        status_t status = init_signals();
        if ( status != STATUS_SUCCESS )
        {
            return status;
        }
        status = init_shared( getpid(), true);
        if ( status != STATUS_SUCCESS )
        {
            return status;
        }
        status = add_user();
        if ( status != STATUS_SUCCESS )
        {
            Destroy();
            return status;
        }

        return STATUS_SUCCESS;
    }

    status_t
    Init( pid_t join_pid)
    {
        status_t status = init_signals();
        if ( status != STATUS_SUCCESS )
        {
            return status;
        }

        sigqueue( join_pid, kSignalJoinRequest, { 0});

        sigset_t  set{};
        siginfo_t info{};
        if ( sigaddset( &set, kSignalJoinApproved) != 0 )
        {
            return STATUS_SIGNAL_ERROR;
        }
        for ( ; ; )
        {
            if ( sigwaitinfo( &set, &info) != -1 )
            {
                break;
            }
            if ( errno != EINTR )
            {
                perror( "sigwaitinfo");
                return STATUS_SIGNAL_ERROR;
            }
        }

        shm_and_sem_id = info.si_pid;
        status = init_shared( shm_and_sem_id, false);
        if ( status != STATUS_SUCCESS )
        {
            return status;
        }

        status = add_user();

        if ( status != STATUS_SUCCESS )
        {
            Destroy();
            return status;
        }

        return STATUS_SUCCESS;
    }

    status_t
    HandleCmd( const std::string &input)
    {
        std::string token{};
        std::istringstream input_stream{ input};

        if ( !(input_stream >> token) )
        {
            help();
            return STATUS_USAGE_ERROR;
        }

        if ( token == "/me" )
        {
            std::cout << "PID: " << getpid() << std::endl;
        } else if ( token == "/tell" )
        {
            pid_t pid_to = -1;
            if ( !(input_stream >> pid_to) )
            {
                help();
                return STATUS_USAGE_ERROR;
            }
            if ( !std::getline( input_stream, token) )
            {
                help();
                return STATUS_USAGE_ERROR;
            }

            while ( isspace(token[0]) )
            {
                token = token.substr( 1);
            }

            token = "\b\b" + std::to_string( getpid()) + ": " + token + "\n> ";

            sem_wait( mutex_);
            send_msg( pid_to, token);
            sem_post( mutex_);
        } else if ( token == "/say" )
        {
            if ( !std::getline( input_stream, token) )
            {
                help();
                return STATUS_USAGE_ERROR;
            }

            while ( isspace(token[0]) )
            {
                token = token.substr( 1);
            }

            token = "\b\b" + std::to_string( getpid()) + ": " + token + "\n> ";

            sem_wait( mutex_);
            for ( size_t i = 0; i != kMaxUsers; ++i )
            {
                if ( (shared_->users[i] != 0) &&
                     (shared_->users[i] != getpid()) )
                {
                    send_msg( shared_->users[i], token);
                }
            }
            sem_post( mutex_);
        } else if ( token == "/help" )
        {
            help();
        } else if ( token == "/exit" )
        {
            return STATUS_EXIT;
        } else
        {
            help();
            return STATUS_USAGE_ERROR;
        }
        return STATUS_SUCCESS;
    }

    void
    Destroy()
    {
        sem_wait( mutex_);
        for ( size_t i = 0 ; i != kMaxUsers; ++i )
        {
            if ( shared_->users[i] == getpid() )
            {
                shared_->users[i] = 0;
                break;
            }
        }
        sem_post( mutex_);
        shm_unlink( shmem_name( shm_and_sem_id).c_str());
        munmap( shared_, sizeof( *shared_));
        sem_unlink( mutex_name( shm_and_sem_id).c_str());
    }

    const msg_t *GetMsg( int id) { return &shared_->messages[id]; }
    void DeleteMsg( int id) { std::memset( &shared_->messages[id], 0, sizeof( shared_->messages[id])); }

private:
    std::string shmem_name( int id) const { return "/tg_shared_memory_" + std::to_string( id); }
    std::string mutex_name( int id) const { return "/tg_mutex_"         + std::to_string( id); }

    status_t
    init_signals()
    {
        struct sigaction sa {};
        sa.sa_flags = SA_SIGINFO | SA_RESTART;

        if ( sigemptyset( &sa.sa_mask) != 0 )
        {
            return STATUS_SIGNAL_ERROR;
        }

        sa.sa_sigaction = join_request_handler;
        if ( sigaction( kSignalJoinRequest, &sa, nullptr) != 0 )
        {
            return STATUS_SIGNAL_ERROR;
        }

        sa.sa_sigaction = join_approve_handler;
        if ( sigaction( kSignalJoinApproved, &sa, nullptr) != 0 )
        {
            return STATUS_SIGNAL_ERROR;
        }

        if ( sigaddset( &sa.sa_mask, SIGINT) != 0 )
        {
            return STATUS_SIGNAL_ERROR;
        }
        sa.sa_sigaction = msg_handler;
        if ( sigaction( kSignalMsg, &sa, nullptr) != 0 )
        {
            return STATUS_SIGNAL_ERROR;
        }
        return STATUS_SUCCESS;
    }

    status_t
    init_shared( pid_t first_pid,
                 bool  need_create_shm)
    {
        std::string shared_memory_name = shmem_name( first_pid);

        int shm_flags = O_RDWR;
        if ( need_create_shm )
        {
            shm_flags |= (O_CREAT | O_TRUNC);
        }

        int shmem_fd = shm_open( shared_memory_name.c_str(), shm_flags, 0666);
        if ( shmem_fd < 0 )
        {
            std::perror( shared_memory_name.c_str());
            return STATUS_SHARED_ERROR;
        }

        if ( need_create_shm )
        {
            if ( ftruncate( shmem_fd, sizeof( *shared_)) != 0 )
            {
                shm_unlink( shared_memory_name.c_str());

                perror( shared_memory_name.c_str());
                return STATUS_SHARED_ERROR;
            }
        }

        shared_t *shared = static_cast<shared_t *>( mmap( nullptr, sizeof( *shared_), PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0));
        if ( shared == MAP_FAILED )
        {
            shm_unlink( shared_memory_name.c_str());

            perror( shared_memory_name.c_str());
            return STATUS_SHARED_ERROR;
        }

        std::string sem_name = mutex_name( first_pid);
        int sem_flags = 0;
        if ( need_create_shm )
        {
            sem_flags |= (O_CREAT | O_EXCL);
        }
        sem_t *mutex = sem_open( sem_name.c_str(), sem_flags, 0666, 1);
        if ( mutex == SEM_FAILED )
        {
            shm_unlink( shared_memory_name.c_str());
            munmap( shared, sizeof( *shared));

            perror( sem_name.c_str());
            return STATUS_SHARED_ERROR;
        }

        mutex_         = mutex;
        shm_and_sem_id = first_pid;
        shared_        = shared;
        valid_         = true;
        shm_fd_        = shmem_fd;

        return STATUS_SUCCESS;
    }

    status_t
    add_user()
    {
        sem_wait( mutex_);

        status_t result = STATUS_NO_SPACE_FOR_USER;
        for ( size_t i = 0; i != kMaxUsers; ++i )
        {
            if ( shared_->users[i] == 0 )
            {
                shared_->users[i] = getpid();
                result = STATUS_SUCCESS;
                break;
            }
        }

        sem_post( mutex_);
        return result;
    }

    // Caller has to lock mutex
    status_t
    send_msg( pid_t              pid,
              const std::string &msg)
    {
        const char *string_data   = msg.c_str();
        size_t      string_length = msg.length();
        if ( string_length > kMaxMsgLen )
        {
            return STATUS_STRING_OVERFLOW;
        }

        status_t result = STATUS_NO_SPACE_FOR_MSG;
        for ( size_t i = 0; i != kMaxMsgNum; ++i )
        {
            if ( shared_->messages[i].length == 0 )
            {
                std::strncpy( shared_->messages[i].data, string_data, string_length);
                shared_->messages[i].length = string_length;
                result = STATUS_SUCCESS;
                break;
            }
        }
        sigqueue( pid, kSignalMsg, { 0});
        return result;
    }

    void
    help()
    {
        std::cout << "/me                   get your PID"                    << std::endl
                  << "/tell <pid> <msg>     send message to particular user" << std::endl
                  << "/say <msg>            broadcast message"               << std::endl
                  << "/exit                 exit from chat"                  << std::endl;
    }

private:
    struct shared_t
    {
        pid_t users[kMaxUsers];
        msg_t messages[kMaxMsgNum];
    };

private:
    bool      valid_  = false;
    shared_t *shared_ = nullptr;
    sem_t    *mutex_  = nullptr;
    int       shm_fd_ = -1;
};

} // !namespace tg

tg::chat_t chat{};

void
join_request_handler( int signum, siginfo_t *siginfo, void *)
{
    sigqueue( siginfo->si_pid, tg::kSignalJoinApproved, { shm_and_sem_id});
}

void
join_approve_handler( int signum, siginfo_t *siginfo, void *)
{
    return ;
}

void
msg_handler( int signum, siginfo_t *siginfo, void *)
{
    const tg::msg_t *msg = chat.GetMsg( siginfo->si_value.sival_int);
    write( STDOUT_FILENO, msg->data, msg->length);
    chat.DeleteMsg( siginfo->si_value.sival_int);
}

int
main( int         argc,
      const char *argv[])
{
    pid_t join_pid = -1;
    if ( argc == 2 )
    {
        join_pid = std::atoi( argv[1]);
    } else if ( argc != 1 )
    {
        std::cerr << "Usage: \"" << argv[0] << " <join pid>\" or \"" << argv[0] << "\"" << std::endl;
        return EXIT_FAILURE;
    }

    tg::status_t status = tg::STATUS_SUCCESS;
    if ( join_pid > 0 )
    {
        status = chat.Init( join_pid);
    } else
    {
        status = chat.Init();
    }

    if ( status != tg::STATUS_SUCCESS )
    {
        return status;
    }

    for ( ; ; )
    {
        std::cout << "> ";
        std::string input {};
        if ( !std::getline( std::cin, input) )
        {
            chat.Destroy();
            return tg::STATUS_INPUT_ERROR;
        }

        status = chat.HandleCmd( input);
        if ( (status != tg::STATUS_SUCCESS) &&
             (status != tg::STATUS_USAGE_ERROR) &&
             (status != tg::STATUS_EXIT) )
        {
            chat.Destroy();
            return status;
        }

        if ( status == tg::STATUS_EXIT )
        {
            chat.Destroy();
            return EXIT_SUCCESS;
        }
    }
}
