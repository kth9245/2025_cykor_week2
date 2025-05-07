#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include "cd.h"
#include "pwd.h"
#include "exit.h"

#define MAX_CMD_LEN 1024
#define MAX_ARG_NUM 64
#define EXIT 0xFF

typedef struct {
    const char *name;
    int (*func)(char **args);
} cmds;

cmds *command_list[] = {
    &(cmds){ "cd", run_cd },
    &(cmds){ "pwd", run_pwd },
    &(cmds){ "exit", run_exit },
    NULL
};

void print_directory() {
    char cwd[1024];
    char hostname[1024];
    struct passwd *pw = getpwuid(getuid());
    char *username = pw ? pw->pw_name : "unknown";
    getcwd(cwd, sizeof(cwd));
    gethostname(hostname, sizeof(hostname));

    printf("\033[1;32m%s\033[0m@\033[1;34m%s\033[0m:\033[1;33m%s\033[0m$ ", username, hostname, cwd);
}

int run_builtin(char **args, int *ret) {
    for (int i = 0; command_list[i] != NULL; i++) {
        if (strcmp(args[0], command_list[i]->name) == 0) {
            *ret = command_list[i]->func(args);
            return 0;
        }
    }
    return -1;
}

int execute_subcmd(char *subcmd) {
    int ret = 0;
    if (!strchr(subcmd, '|')) {
        char *args[64];
        int i = 0;
        char *token = strtok(subcmd, " ");
        while (token && i < 63) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;
        if (args[0] == NULL) return 0;

        if (run_builtin(args, &ret) == 0) return ret;

        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args);
            perror("execvp");
            return 1;
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
        } else {
            perror("fork");
            return 1;
        }
    }

    char *cmds[16];
    int count = 0;
    char *saveptr;
    char *part = strtok_r(subcmd, "|", &saveptr);
    while (part && count < 15) {
        while (*part == ' ') part++;
        char *end = part + strlen(part) - 1;
        while (end > part && *end == ' ') *end-- = '\0';
        cmds[count++] = part;
        part = strtok_r(NULL, "|", &saveptr);
    }
    cmds[count] = NULL;

    int prev_fd = -1;
    for (int i = 0; i < count; i++) {
        int pipefd[2];
        if (i < count - 1) pipe(pipefd);

        pid_t pid = fork();
        if (pid == 0) {
            if (prev_fd != -1) {
                dup2(prev_fd, 0);
                close(prev_fd);
            }
            if (i < count - 1) {
                dup2(pipefd[1], 1);
                close(pipefd[0]);
                close(pipefd[1]);
            }

            char *args[64];
            int j = 0;
            char *tok = strtok(cmds[i], " ");
            while (tok && j < 63) {
                args[j++] = tok;
                tok = strtok(NULL, " ");
            }
            args[j] = NULL;
            if (args[0] == NULL) return 1;
            execvp(args[0], args);
            perror("execvp");
            return 1;
        } else {
            if (prev_fd != -1) close(prev_fd);
            if (i < count - 1) {
                close(pipefd[1]);
                prev_fd = pipefd[0];
            }
            waitpid(pid, NULL, 0);
        }
    }
    return 0;
}

void parse_cmd(char* cmd) {
    unsigned int ret_val = 0;
    unsigned int op_val = 0;

    while (cmd) {
        char *and_pos = strstr(cmd, "&&");
        char *or_pos = strstr(cmd, "||");
        char *split_pos = NULL;
    
        if (and_pos && (!or_pos || and_pos < or_pos)) {
            split_pos = and_pos;
            op_val = 0;
        } else if (or_pos) {
            split_pos = or_pos;
            op_val = 1;
        }
    
        char *subcmd;
        if (split_pos) {
            subcmd = strndup(cmd, split_pos - cmd);
        } else {
            subcmd = strdup(cmd);
        }
        ret_val = execute_subcmd(subcmd);
        free(subcmd);
        if (ret_val == 0xFF) exit(1);
        
        if (op_val != ret_val) break;

        cmd = split_pos ? split_pos + 2 : NULL;
    }
}

int main() {
    char line[MAX_CMD_LEN];

    while (1) {
        print_directory();
        memset(line, 0, sizeof(line));
        if (!fgets(line, sizeof(line) - 1, stdin)) break;
        line[strcspn(line, "\n")] = 0;
        char *saveptr;
        char *cmd = strtok_r(line, ";", &saveptr);
        while (cmd) {
            parse_cmd(cmd);
            cmd = strtok_r(NULL, ";", &saveptr);
        }
    }
    return 0;
}