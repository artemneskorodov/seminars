#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

typedef struct
{
    bool flag_all;
    bool flag_long;
    bool flag_inode;     // TODO
    bool flag_numeric;   // TODO
    bool flag_recursive; // TODO
    bool flag_directory; // TODO
} ls_mode_t;

typedef struct
{
    struct dirent ent;
    const char *user_name;
    const char *group_name;
    struct stat st;
} ent_info_t;

typedef struct
{
    size_t max_sym_nlink;
    size_t max_sym_uname;
    size_t max_sym_gname;
    size_t max_sym_size;
    size_t max_sym_day;
} ls_limit_t;

static const size_t kStartBufferSize = 4;

int get_flags(int argc, char **argv, ls_mode_t *mode);
int compare_ents(const void *a, const void *b);
int create_ents_arr(const char *path, ent_info_t **ents, size_t *size);
int get_limits(ent_info_t *ents, size_t ents_size, ls_limit_t *limits, ls_mode_t *mode);
int print_file_info(ent_info_t *ent, ls_mode_t *mode, ls_limit_t *limits, const char *dirname, int depth);
int print_dir_info(ls_mode_t *mode, const char *dirname, int depth);

int
main(int    argc,
     char **argv)
{
    // Reading flags
    ls_mode_t mode = {};
    if (get_flags(argc, argv, &mode) != 0)
    {
        return EXIT_FAILURE;
    }

    print_dir_info(&mode, ".", 0);

    return EXIT_SUCCESS;
}

int
print_dir_info(ls_mode_t  *mode,
               const char *dirname,
               int         depth)
{
    ent_info_t *ents = NULL;
    size_t ents_size = 0;
    if (create_ents_arr(dirname, &ents, &ents_size) != 0)
    {
        return EXIT_FAILURE;
    }

    ls_limit_t limits = {};
    if (get_limits(ents, ents_size, &limits, mode) != 0)
    {
        free(ents);
        return EXIT_FAILURE;
    }

    int file_name_offset = depth;
    for ( size_t ent = 0; ent != ents_size; ++ent )
    {
        print_file_info(&ents[ent], mode, &limits, dirname, file_name_offset);

        if ((mode->flag_recursive) &&
            (S_ISDIR(ents[ent].st.st_mode)) &&
            (ents[ent].ent.d_name[0] != '.'))
        {
            printf(":");
            char filename_buffer[256] = {};
            snprintf(filename_buffer, 256, "%s/%s", dirname, ents[ent].ent.d_name);
            print_dir_info(mode, filename_buffer, depth + 1);
        } else
        {
            putchar(' ');
        }

        if (mode->flag_long || mode->flag_recursive)
        {
            putchar('\n');
        } else
        {
            file_name_offset = 0;
        }
    }

    free(ents);
}

int
print_file_info(ent_info_t *ent,
                ls_mode_t  *mode,
                ls_limit_t *limits,
                const char *dirname,
                int         depth)
{
    if (ent->ent.d_name[0] == '.' && !mode->flag_all)
    {
        return 0;
    }

    for ( int i = 0; i != depth; ++i )
    {
        putchar('\t');
    }

    if (!mode->flag_long)
    {
        printf("%s", ent->ent.d_name);
        return 0;
    }

    if      (S_ISREG(ent->st.st_mode)) putchar('-');
    else if (S_ISDIR(ent->st.st_mode)) putchar('d');
    else if (S_ISLNK(ent->st.st_mode)) putchar('l');
    else    printf("(unexpected st.st_mode)");

    // User
    putchar((ent->st.st_mode & S_IRUSR) ? 'r' : '-');
    putchar((ent->st.st_mode & S_IWUSR) ? 'w' : '-');
    putchar((ent->st.st_mode & S_IXUSR) ? 'x' : '-');
    // Group
    putchar((ent->st.st_mode & S_IRGRP) ? 'r' : '-');
    putchar((ent->st.st_mode & S_IWGRP) ? 'w' : '-');
    putchar((ent->st.st_mode & S_IXGRP) ? 'x' : '-');
    // Others
    putchar((ent->st.st_mode & S_IROTH) ? 'r' : '-');
    putchar((ent->st.st_mode & S_IWOTH) ? 'w' : '-');
    putchar((ent->st.st_mode & S_IXOTH) ? 'x' : '-');

    // nlinks, user name, group name, size
    printf(" %*d "
           "%*s "
           "%*s "
           "%*d ",
           limits->max_sym_nlink, ent->st.st_nlink,
           limits->max_sym_uname, ent->user_name,
           limits->max_sym_gname, ent->group_name,
           limits->max_sym_size,  ent->st.st_size);

    // Last change time
    time_t mtime_val = ent->st.st_mtime;
    struct tm *timeinfo = localtime(&mtime_val);
    char buffer[100];
    size_t pos = strftime(buffer, sizeof(buffer), "%b ", timeinfo); // %d %H:%M
    int mday = timeinfo->tm_mday;
    size_t mday_size = 0;
    do
    {
        mday_size++;
        mday /= 10;
    } while (mday != 0);
    memset(buffer + pos, limits->max_sym_day - mday_size, ' ');
    pos += limits->max_sym_day - mday_size;
    strftime(buffer + pos, sizeof(buffer) - pos, "%d %H:%M", timeinfo);
    printf("%s ", buffer);

    // Name
    printf("%s", ent->ent.d_name);

    // Link path
    if (S_ISLNK(ent->st.st_mode))
    {
        char buffer[100] = {};
        ssize_t len = readlink(ent->ent.d_name, buffer, sizeof(buffer) - 1);
        if (len == -1)
        {
            perror(ent->ent.d_name);
            return 0;
        }
        printf(" -> %s", buffer);
    }
}

int
get_flags(int         argc,
          char      **argv,
          ls_mode_t  *mode)
{
    struct option long_options[] = {
        {      "all", no_argument, NULL, 'a'},
        {"directory", no_argument, NULL, 'd'},
        {     "long", no_argument, NULL, 'l'},
        {    "inode", no_argument, NULL, 'i'},
        {  "numeric", no_argument, NULL, 'n'},
        {"recursive", no_argument, NULL, 'R'},
        {0, 0, 0, 0},
    };


    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "adlinR", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a': mode->flag_all       = true; break;
            case 'l': mode->flag_long      = true; break;
            case 'i': mode->flag_inode     = true; break;
            case 'n': mode->flag_numeric   = true; break;
            case 'R': mode->flag_recursive = true; break;
            case '?':
            default:
                fprintf(stderr, "getopt failed.\n");
                return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

int
compare_ents(const void *a_void,
             const void *b_void)
{
    const ent_info_t *a = (const ent_info_t *)a_void;
    const ent_info_t *b = (const ent_info_t *)b_void;

    int a_is_special = 0;
    int b_is_special = 0;

    if      (strcmp(a->ent.d_name, "." ) == 0) a_is_special = 1;
    else if (strcmp(a->ent.d_name, "..") == 0) a_is_special = 2;

    if      (strcmp(b->ent.d_name, "." ) == 0) b_is_special = 1;
    else if (strcmp(b->ent.d_name, "..") == 0) b_is_special = 2;

    if (a_is_special && b_is_special) return a_is_special - b_is_special;
    if (a_is_special)                 return -1;
    if (b_is_special)                 return 1;

    int offset_a = 0;
    int offset_b = 0;

    if (a->ent.d_name[0] == '.') offset_a = 1;
    if (b->ent.d_name[0] == '.') offset_b = 1;

    return strcmp(a->ent.d_name + offset_a, b->ent.d_name + offset_b);
}

int
create_ents_arr(const char  *path,
                ent_info_t **ents,
                size_t      *size)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        perror(path);
        return EXIT_FAILURE;
    }

    size_t ents_capacity = kStartBufferSize;
    *ents = (ent_info_t*)calloc(ents_capacity, sizeof(**ents));
    if (*ents == NULL)
    {
        perror("calloc");
        return EXIT_FAILURE;
    }
    size_t ents_size = 0;
    while (true)
    {
        if (ents_size == ents_capacity)
        {
            ent_info_t *new_ents = (ent_info_t *)realloc(*ents, 2 * ents_capacity * sizeof(**ents));
            if (new_ents == NULL)
            {
                perror("realloc");
                free(*ents);
                *ents = NULL;
                *size = 0;
                closedir(dir);
                return EXIT_FAILURE;
            }
            *ents = new_ents;
            ents_capacity *= 2;
        }

        struct dirent *ent = readdir(dir);
        if (ent == NULL)
        {
            break;
        }
        (*ents)[ents_size].ent = *ent;

        char filename_buffer[256] = {};
        snprintf(filename_buffer, 256, "%s/%s", path, (*ents)[ents_size].ent.d_name);

        if ((*ents)[ents_size].ent.d_type == DT_LNK)
        {
            if (lstat(filename_buffer, &(*ents)[ents_size].st) != 0)
            {
                perror(filename_buffer);
                continue;
            }
        } else
        {
            if (stat(filename_buffer, &(*ents)[ents_size].st) != 0)
            {
                perror(filename_buffer);
                continue;
            }
        }
        struct passwd *pw = getpwuid((*ents)[ents_size].st.st_uid);
        struct group  *gr = getgrgid((*ents)[ents_size].st.st_gid);
        (*ents)[ents_size].group_name = gr->gr_name;
        (*ents)[ents_size].user_name  = pw->pw_name;
        ents_size++;
    }

    *size = ents_size;

    qsort(*ents, ents_size, sizeof(**ents), compare_ents);
    closedir(dir);
    return EXIT_SUCCESS;
}

int
get_limits(ent_info_t *ents,
           size_t      ents_size,
           ls_limit_t *limits,
           ls_mode_t  *mode)
{
    if (!mode->flag_long)
    {
        return EXIT_SUCCESS;
    }
    for (size_t i = 0; i != ents_size; ++i)
    {
        if (ents[i].ent.d_name[0] == '.' && !mode->flag_all)
        {
            continue;
        }
        nlink_t st_nlink = ents[i].st.st_nlink;
        size_t sym_nlink = 0;
        do
        {
            sym_nlink++;
            st_nlink /= 10;
        } while (st_nlink != 0);

        size_t sym_uname = strlen(ents[i].user_name);

        size_t sym_gname = strlen(ents[i].group_name);

        size_t sym_size = 0;
        off_t st_size = ents[i].st.st_size;
        do
        {
            sym_size++;
            st_size /= 10;
        } while (st_size != 0);

        time_t mtime_val = ents[i].st.st_mtime;
        struct tm *timeinfo = localtime(&mtime_val);
        char buffer[100];
        strftime(buffer, sizeof(buffer), "%d", timeinfo);
        size_t sym_day = strlen(buffer);

        if (sym_nlink > limits->max_sym_nlink) limits->max_sym_nlink = sym_nlink;
        if (sym_uname > limits->max_sym_uname) limits->max_sym_uname = sym_uname;
        if (sym_gname > limits->max_sym_gname) limits->max_sym_gname = sym_gname;
        if (sym_size  > limits->max_sym_size ) limits->max_sym_size  = sym_size;
        if (sym_day   > limits->max_sym_day  ) limits->max_sym_day   = sym_day;
    }
    return EXIT_SUCCESS;
}
