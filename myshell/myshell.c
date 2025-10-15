#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BUFFER_SIZE (1024)
#define MAX_ARGV_SIZE   (64)
#define MAX_COMMANDS    (16)

typedef enum
{
    MYSHELL_SUCCESS,
    MYSHELL_SYNTAX_ERROR,
    MYSHELL_OPEN_ERROR,
    MYSHELL_PIPE_ERROR,
    MYSHELL_UNSUPPORTED_ARGV,
    MYSHELL_EXEC_ERROR,
    MYSHELL_UNSUPPORTED_COMMANDS_NUM,
    MYSHELL_ERROR_READING_USER_INPUT,
} myshell_error_t;

static void
get_redirection(char        *position,
                const char **input_path,
                const char **output_path);

static myshell_error_t
run_commands(char       **commands,
             size_t       commands_num,
             const char  *input_file,
             const char  *output_file);

static myshell_error_t
parse_argv(char  *command,
           char **argv);

int
main(int         argc,
     const char *argv[])
{
    char buffer[MAX_BUFFER_SIZE];
    char *commands[MAX_COMMANDS];
    myshell_error_t error_code;

    while (true)
    {
        printf ("~> ");
        fflush (stdout);

        if (!fgets(buffer, sizeof (buffer), stdin))
        {
            fprintf(stderr, "%s: Error reading user input.\n", argv[0]);
            return (int)MYSHELL_ERROR_READING_USER_INPUT;
        }

        // Deleting '\n' in the end of line
        buffer[strcspn(buffer, "\n")] = 0;
        if (strlen(buffer) == 0)
        {
            continue;
        }
        if (strcmp(buffer, "exit") == 0)
        {
            break;
        }

        // Parsing line to conveyor commands
        char *position      = buffer;
        size_t commands_num = 1;
        commands[0]         = position;
        while (*position != '\0')
        {
            if (*position == '|')
            {
                *position = '\0';
                if (commands_num >= MAX_COMMANDS)
                {
                    fprintf(stderr, "Sorry, this shell supports only %d commands in conveyor.\n");
                    return (int)MYSHELL_UNSUPPORTED_COMMANDS_NUM;
                }
                commands[commands_num++] = position + 1;
            }
            position++;
        }

        // Getting input and output files for first and last command
        const char *input_file  = NULL;
        const char *output_file = NULL;
        for (size_t i = 0; i < commands_num; ++i)
        {
            const char *current_input  = NULL;
            const char *current_output = NULL;
            get_redirection(commands[i], &current_input, &current_output);
            if (current_input != NULL)
            {
                if (i != 0)
                {
                    fprintf(stderr, "Only first command can have input redirection.\n");
                    return (int)MYSHELL_SYNTAX_ERROR;
                }
                input_file = current_input;
            }
            if (current_output != NULL)
            {
                if (i + 1 != commands_num)
                {
                    fprintf(stderr, "Only last command can have output redirection.\n");
                    return (int)MYSHELL_SYNTAX_ERROR;
                }
                output_file = current_output;
            }
        }

        error_code = run_commands(commands, commands_num, input_file, output_file);
        if (error_code != MYSHELL_SUCCESS)
        {
            return (int)error_code;
        }
    }

    return EXIT_SUCCESS;
}

static void
get_redirection(char        *position,
                const char **input_path,
                const char **output_path)
{
    while (*position != '\0')
    {
        if (*position == '<')
        {
            *position = '\0';
            position++;
            while (isspace(*position))
            {
                *position = '\0';
                position++;
            }
            *input_path = position;
            while (!isspace(*position) && *position != '\0' && *position != '>')
            {
                position++;
            }
            if (*position != '>')
            {
                *position = '\0';
                position++;
            }
            continue;
        }
        if (*position == '>')
        {
            *position = '\0';
            position++;
            while (isspace(*position))
            {
                *position = '\0';
                position++;
            }
            *output_path = position;
            while (!isspace(*position) && *position != '\0' && *position != '<')
            {
                position++;
            }
            if (*position != '<')
            {
                *position = '\0';
                position++;
            }
            continue;
        }
        position++;
    }
}

static myshell_error_t
run_commands(char       **commands,
             size_t       commands_num,
             const char  *input_file,
             const char  *output_file)
{
    int prev_out_fd = -1;
    int pipefds[2];
    size_t children_num = 0;
    for (size_t i = 0; i < commands_num; ++i)
    {
        char *argv[MAX_ARGV_SIZE];
        myshell_error_t error_code = parse_argv(commands[i], argv);
        if (error_code != MYSHELL_SUCCESS)
        {
            if (prev_out_fd != -1)
            {
                close (prev_out_fd);
            }
            return error_code;
        }
        // Creating pipe
        if (i + 1 != commands_num)
        {
            if (pipe (pipefds) != 0)
            {
                perror ("Pipe error.\n");
                return MYSHELL_PIPE_ERROR;
            }
        }
        children_num++;
        pid_t pid = fork ();
        if (pid == 0)
        {
            if (i == 0 && input_file != NULL)
            {
                int input_fd = open(input_file, O_RDONLY);
                if (input_fd < 0)
                {
                    perror(input_file);
                    return MYSHELL_OPEN_ERROR;
                }
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }

            if (i + 1 == commands_num && output_file != NULL)
            {
                int output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0664);
                if (output_fd < 0)
                {
                    perror(output_file);
                    return MYSHELL_OPEN_ERROR;
                }
                dup2(output_fd, STDOUT_FILENO);
                close(output_fd);
            }

            if (prev_out_fd != -1)
            {
                dup2(prev_out_fd, STDIN_FILENO);
                close(prev_out_fd);
            }

            if (i + 1 != commands_num)
            {
                dup2(pipefds[1], STDOUT_FILENO);
                close(pipefds[0]);
                close(pipefds[1]);
            }

            execvp(argv[0], argv);
            perror(argv[0]);
            return MYSHELL_EXEC_ERROR;
        } else
        {
            children_num++;

            if (prev_out_fd != -1)
            {
                close(prev_out_fd);
                prev_out_fd = -1;
            }

            if (i + 1 != commands_num)
            {
                prev_out_fd = pipefds[0];
                close(pipefds[1]);
            }
        }
    }

    for (size_t i = 0; i < children_num; i++) {
        wait (NULL);
    }
    return MYSHELL_SUCCESS;
}

static myshell_error_t
parse_argv(char  *command,
           char **argv)
{
    bool in_word = false;
    size_t argv_index = 0;
    for ( ; *command != '\0'; command++) {
        if (!isspace(*command)) {
            if (!in_word) {
                if (argv_index >= MAX_ARGV_SIZE) {
                    fprintf(stderr, "Unsupported argv size amount.\n");
                    return MYSHELL_UNSUPPORTED_ARGV;
                }
                argv[argv_index++] = command;
                in_word            = true;
            }
        } else {
            *command = '\0';
            in_word  = false;
        }
    }
    argv[argv_index] = NULL;
    return MYSHELL_SUCCESS;
}
