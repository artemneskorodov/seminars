#include <stdio.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdbool.h>

#define MAX_HUNTERS         (5)
#define COOK_MTYPE          (MAX_HUNTERS + 1)
#define SND_TO_HUNTER( id_) (id_ + 1)

static const int kFoodInHunter = 2;

typedef enum
{
    MSG_FOOD      = 1 << 0,
    MSG_KILLED    = 1 << 1,
    MSG_COOK_DEAD = 1 << 2,
} msg_val_t;

typedef struct
{
    long mtype;
    msg_val_t value;
    int id_from;
} msg_t;

int hunter( int id, int msg_id);
int cook( int msg_id);

int
main()
{
    // Creating msg queue
    int msg_id = msgget( IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    if ( msg_id < 0 )
    {
        perror("Message queue error");
        return EXIT_FAILURE;
    }

    for ( int i = 0; i < MAX_HUNTERS; ++i )
    {
        pid_t pid = fork();
        if ( pid == 0 )
        {
            return hunter( i, msg_id);
        }
    }

    cook( msg_id);
    for ( int i = 0; i != MAX_HUNTERS; ++i )
    {
        wait( NULL);
    }

    msgctl(msg_id, IPC_RMID, 0);
}

int
hunter( int id, int msg_id)
{
    srand( getpid());
    bool success = true;
    for ( ; ; )
    {
        msg_t msg = {};

        // Охотимся
        bool got_food;
        if ( success )
        {
            // 50 %
            got_food = (rand() % 2) == 0;
        } else
        {
            // 33 %
            got_food = (rand() % 3) == 0;
        }

        msg.mtype   = COOK_MTYPE;
        msg.value   = got_food ? MSG_FOOD : 0;
        msg.id_from = id;

        // Отправляем повару данные о добытой еде
        if ( msgsnd( msg_id, &msg, sizeof( msg) - sizeof( msg.mtype), 0) < 0 )
        {
            perror( "msgsnd failed");
            return EXIT_FAILURE;
        }

        // По возможности получаем пищу от повара и узнаем, не убил ли он нас
        if ( msgrcv( msg_id, &msg, sizeof( msg) - sizeof( msg.mtype), SND_TO_HUNTER( id), 0) < 0 )
        {
            perror( "msgrcv failed");
            return EXIT_FAILURE;
        }

        if ( msg.value & MSG_COOK_DEAD )
        {
            return EXIT_SUCCESS;
        }

        success = (msg.value & MSG_FOOD);

        if ( msg.value & MSG_KILLED )
        {
            return EXIT_SUCCESS;
        }
    }
}

int
cook( int msg_id)
{
    printf( "[COOK] All hunters are created\n");
    int food_count = 0;
    int hunters_count = MAX_HUNTERS;
    bool is_alive[MAX_HUNTERS] = {};
    for ( int i = 0; i != MAX_HUNTERS; ++i )
    {
        is_alive[i] = true;
    }

    int days_count = 0;

    for ( ; ; )
    {
        printf( "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\n");
        printf( "DAY %d\n", days_count);
        days_count++;

        // Получаем еду от живых охотников
        printf( "Getting food from hunters\n");
        int brought_food[MAX_HUNTERS] = {0};
        for ( int i = 0; i != hunters_count; ++i )
        {
            msg_t msg = {};
            if ( msgrcv( msg_id, &msg, sizeof( msg) - sizeof( msg.mtype), COOK_MTYPE, 0) < 0 )
            {
                perror( "msgrcv failed");
                return EXIT_FAILURE;
            }
            if ( msg.value & MSG_FOOD )
            {
                printf( "\tOne piece of food from hunter %d\n", msg.id_from);
                brought_food[msg.id_from] = 1;
                food_count++;
            } else
            {
                printf( "\tNo food from hunter %d\n", msg.id_from);
                brought_food[msg.id_from] = 0;
            }
        }

        // Раздаем еду
        bool cook_had_food = false;
        printf( "There is %d pieces of food, giving it to hunters and me\n", food_count);
        if ( food_count > 0 )
        {
            cook_had_food = true;
            food_count--;
            printf( "\tTook one piece\n");
        } else
        {
            printf( "\tNo food even for me\n");
        }

        // Если не всем хватило еды, выбираем, кого можно убить (первый кто не принес еды)
        int hunter_to_kill = -1;
        if ( food_count < hunters_count || !cook_had_food )
        {
            for ( int i = 0; i != MAX_HUNTERS; ++i )
            {
                if ( !is_alive[i] )
                {
                    continue;
                }
                if ( brought_food[i] == 0 )
                {
                    hunter_to_kill = i;
                    break;
                }
            }
            // Если все охотники принесли, но все равно не хватило, убиваем кого-то
            if ( hunter_to_kill == -1 )
            {
                for ( int i = 0; i != MAX_HUNTERS; ++i )
                {
                    if ( !is_alive[i] )
                    {
                        continue;
                    }
                    hunter_to_kill = i;
                    break;
                }
            }

            food_count += kFoodInHunter;
            hunters_count--;

            if ( !cook_had_food )
            {
                // Повар забирает себе кусок еды если ему еще не досталось
                food_count--;
            }

            printf( "Hunter %d will be killed\n", hunter_to_kill);
        }

        // Отправляем всем сообщения, не убили ли мы их случайно, и досталась ли им еда
        for ( int i = 0; i != MAX_HUNTERS; ++i )
        {
            if ( !is_alive[i] )
            {
                continue;
            }

            msg_t msg = {};
            msg.mtype = SND_TO_HUNTER( i);

            if ( i == hunter_to_kill )
            {
                is_alive[i] = false;
                printf( "\tKilling hunter %d\n", i);
                msg.value |= MSG_KILLED;
            }

            if ( food_count > 0 && i != hunter_to_kill )
            {
                printf( "\tOne piece of food to hunter %d\n", i);
                msg.value |= MSG_FOOD;
                food_count--;
            } else
            {
                printf( "\tThere is no food for hunter %d\n", i);
            }
            if ( msgsnd( msg_id, &msg, sizeof( msg) - sizeof( msg.mtype), 0) < 0 )
            {
                perror( "msgsnd failed");
                return EXIT_FAILURE;
            }
        }

        // Повар дохнет если еды так и не досталось
        if ( !cook_had_food )
        {
            for ( int i = 0; i != MAX_HUNTERS; ++i )
            {
                if ( !is_alive[i] )
                {
                    continue;
                }
                msg_t msg = {};
                msg.mtype = SND_TO_HUNTER( i);
                msg.value = MSG_COOK_DEAD;
                if ( msgsnd( msg_id, &msg, sizeof( msg) - sizeof( msg.mtype), 0) < 0 )
                {
                    perror( "msgsnd failed");
                    return EXIT_FAILURE;
                }
            }
            printf( "Cook is dead\n");
            printf( "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\n");
            return EXIT_SUCCESS;
        }
    }
}
