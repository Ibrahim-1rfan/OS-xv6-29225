#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

void find(char *path, char *filename, int exec_mode, char *exec_argv[], int exec_argc);

void
run_exec(char *filepath, char *exec_argv[], int exec_argc)
{
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "find: fork failed\n");
        exit(1);
    }
    
    if (pid == 0) {
        // Child process - execute the command
        char *new_argv[MAXARG];
        int i;
        
        // Copy the command and its arguments
        for (i = 0; i < exec_argc; i++) {
            new_argv[i] = exec_argv[i];
        }
        
        // Add the file path as the last argument
        new_argv[i] = filepath;
        new_argv[i + 1] = 0;  // Null terminate
        
        // Execute the command
        exec(new_argv[0], new_argv);
        
        // If exec returns, it failed
        fprintf(2, "find: exec %s failed\n", new_argv[0]);
        exit(1);
    } else {
        // Parent process - wait for child to complete
        wait(0);
    }
}

void
find(char *path, char *filename, int exec_mode, char *exec_argv[], int exec_argc)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        // Extract just the filename from the path
        for (p = path + strlen(path); p >= path && *p != '/'; p--);
        p++;
        
        // Check if filename matches (or if no filename specified, match all)
        if (strcmp(filename, "") == 0 || strcmp(p, filename) == 0) {
            if (exec_mode) {
                run_exec(path, exec_argv, exec_argc);
            } else {
                printf("%s\n", path);
            }
        }
        break;

    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0) {
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            // Skip . and ..
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            find(buf, filename, exec_mode, exec_argv, exec_argc);
        }
        break;
    }
    close(fd);
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(2, "Usage: find <path> [filename] [-exec command...]\n");
        exit(1);
    }

    char *path = argv[1];
    char *filename = "";
    int exec_mode = 0;
    char *exec_argv[MAXARG];
    int exec_argc = 0;

    // Parse arguments
    if (argc == 2) {
        // find path (no filename specified, find all files)
        filename = "";
    } else if (argc >= 3 && strcmp(argv[2], "-exec") == 0) {
        // find path -exec cmd arg1 ...
        filename = "";  // find all files
        exec_mode = 1;
        if (argc < 4) {
            fprintf(2, "find: -exec requires a command\n");
            exit(1);
        }
        for (int i = 3; i < argc; i++) {
            if (exec_argc >= MAXARG - 1) {
                fprintf(2, "find: too many arguments\n");
                exit(1);
            }
            exec_argv[exec_argc++] = argv[i];
        }
    } else if (argc >= 4 && strcmp(argv[3], "-exec") == 0) {
        // find path filename -exec cmd arg1 ...
        filename = argv[2];
        exec_mode = 1;
        if (argc < 5) {
            fprintf(2, "find: -exec requires a command\n");
            exit(1);
        }
        for (int i = 4; i < argc; i++) {
            if (exec_argc >= MAXARG - 1) {
                fprintf(2, "find: too many arguments\n");
                exit(1);
            }
            exec_argv[exec_argc++] = argv[i];
        }
    } else {
        // find path filename
        filename = argv[2];
    }

    exec_argv[exec_argc] = 0;  // Null terminate the argument array

    find(path, filename, exec_mode, exec_argv, exec_argc);
    exit(0);
}
