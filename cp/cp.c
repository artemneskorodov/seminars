#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <stdbool.h>

typedef enum
{
    CP_STATE_SUCCESS,
    CP_STATE_FAIL,
} cp_state_t;

typedef struct cp_mode_t
{
    bool verbose;
    bool force;
    bool interactive;
    char *dst;
    size_t dst_sz;
    bool is_dst_folder;
    size_t folder_sz;
    size_t dst_argv_index;
} cp_mode_t;

#define BUFFER_SIZE (4)

cp_state_t
cp_parse_arguments(int                       argc,
                   const char *const *const  argv,
                   cp_mode_t                *mode);

const char *
get_base(const char *path);

cp_state_t
copy_file(const char *src,
          const char *dst,
          cp_mode_t  *mode);

int
main(int                      argc,
     const char *const *const argv)
{
    // Parsing arguments
    cp_mode_t mode = {0};
    if (cp_parse_arguments(argc, argv, &mode) != CP_STATE_SUCCESS)
    {
        return EXIT_FAILURE;
    }
    for (int i = 1; i < mode.dst_argv_index; ++i)
    {
        if (*argv[i] == '-')
        {
            continue;
        }
        char *dst = NULL;
        if (mode.is_dst_folder)
        {
            dst = mode.dst;
            const char *base = get_base(argv[i]);
            memset(dst + mode.folder_sz, 0, mode.dst_sz - mode.folder_sz);
            strcpy(dst + mode.folder_sz, base);
        } else
        {
            dst = (char *)argv[mode.dst_argv_index];
        }
        if (copy_file(argv[i], dst, &mode) != CP_STATE_SUCCESS)
        {
            if (mode.is_dst_folder)
            {
                free(mode.dst);
            }
            return EXIT_FAILURE;
        }
    }
    if (mode.is_dst_folder)
    {
        free(mode.dst);
    }
    return EXIT_SUCCESS;
}

cp_state_t
cp_parse_arguments(int                       argc,
                   const char *const *const  argv,
                   cp_mode_t                *mode)
{
    // Parsing command line input
    struct option long_options[] =
    {
        {.name =     "verbose", .has_arg = no_argument, .flag = NULL, .val = 'v'},
        {.name =       "force", .has_arg = no_argument, .flag = NULL, .val = 'f'},
        {.name = "interactive", .has_arg = no_argument, .flag = NULL, .val = 'i'},
    };
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, (char* const*)argv, "vfi", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
            case 'v': mode->verbose     = true; break;
            case 'f': mode->force       = true; break;
            case 'i': mode->interactive = true; break;
            case '?':
            default:
            {
                return CP_STATE_FAIL;
            }
        }
    }

    // Getting dst index in argv
    mode->dst_argv_index = argc - 1;
    while (argv[mode->dst_argv_index][0] == '-')
    {
        mode->dst_argv_index--;
    }
    mode->folder_sz = strlen(argv[mode->dst_argv_index]);

    size_t max_length = 0;
    int sources_num = 0;
    // Checking every src if it is directory or does not exist
    size_t options_number = 0;
    for (int i = 1; i < mode->dst_argv_index; i++)
    {
        if (argv[i][0] == '-')
        {
            options_number++;
            continue;
        }

        // Checking if name is bigger then others
        size_t name_length = strlen(argv[i]);
        if (name_length > max_length)
        {
            max_length = name_length;
        }
        sources_num++;
    }

    // Defining if dst is folder
    if (sources_num < 1)
    {
        printf("%s: usage: %s [OPTIONS] [SRC's] [DST]", argv[0], argv[0]);
        return CP_STATE_FAIL;
    } else if (sources_num == 1)
    {
        // dst is folder if it already exists and it is folder
        struct stat st;
        if (stat(argv[mode->dst_argv_index], &st) == 0 &&
            S_ISDIR(st.st_mode))
        {
            mode->is_dst_folder = true;
        } else
        {
            mode->is_dst_folder = false;
        }
    } else
    {
        // If number of sources is bigger then 1, dst is folder
        mode->is_dst_folder = true;
    }

    // If dst is a folder, we need to create a buffer to add file base names to folder
    if (mode->is_dst_folder)
    {
        mode->dst_sz = mode->folder_sz + max_length + 2;
        mode->dst = (char *)calloc(mode->dst_sz, sizeof(*mode->dst));
        if (mode->dst == NULL)
        {
            printf("%s: error while allocating memory\n", argv[0]);
            return CP_STATE_FAIL;
        }

        // Saving folder to use in loop
        memcpy(mode->dst, argv[mode->dst_argv_index], mode->folder_sz);
        if (mode->dst[mode->folder_sz - 1] != '/')
        {
            mode->dst[mode->folder_sz] = '/';
            mode->folder_sz++;
        }
    }
    return CP_STATE_SUCCESS;
}

const char *
get_base(const char *path)
{
    const char *pos = path + strlen(path);
    while (pos != path)
    {
        if (*pos == '/')
        {
            return pos + 1;
        }
        pos--;
    }
    return pos;
}

cp_state_t
copy_file(const char *src,
          const char *dst,
          cp_mode_t  *mode)
{
    // Opening src
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0)
    {
        perror(src);
        return CP_STATE_FAIL;
    }

    // Checking if dst exists
    struct stat st;
    bool dst_exists = false;
    if (stat(dst, &st) == 0)
    {
        dst_exists = true;
        if (!mode->force && !mode->interactive)
        {
            printf("Cannot copy %s: file already exists, use --force or --interactive\n", dst);
            close(src_fd);
            return CP_STATE_SUCCESS;
        } else if (!mode->force && mode->interactive)
        {
            printf("%s: file already exists. Want to override?[y/n]: ");
            int choice = getchar();
            // Clearing stdio data
            int c;
            while ((c = getchar()) != '\n' && c != EOF);

            if (choice != 'Y' && choice != 'y')
            {
                close(src_fd);
                return CP_STATE_SUCCESS;
            }
        } // else mode->force, overriding
    }

    // Opening dst
    int dst_fd;
    mode_t default_permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    if (!dst_exists)
    {
        dst_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, default_permissions);
    } else
    {
        dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, default_permissions);
    }
    if (dst_fd < 0)
    {
        close(src_fd);
        perror(dst);
        return CP_STATE_FAIL;
    }

    char buffer[BUFFER_SIZE];
    while (true)
    {
        ssize_t read_bytes = read(src_fd, buffer, BUFFER_SIZE);
        if (read_bytes < 0)
        {
            perror(src);
            close(src_fd);
            close(dst_fd);
            return CP_STATE_FAIL;
        } else if (read_bytes == 0)
        {
            break;
        }

        ssize_t written = 0;
        while (written < read_bytes)
        {
            ssize_t write_bytes = write(dst_fd, buffer, read_bytes);
            if (write_bytes < 0)
            {
                perror(dst);
                close(src_fd);
                close(dst_fd);
                return CP_STATE_FAIL;
            }
            written += write_bytes;
        }
    }
    close(src_fd);
    close(dst_fd);
    if (mode->verbose)
    {
        printf("'%s' -> '%s'\n", src, dst);
    }
    return CP_STATE_SUCCESS;
}
