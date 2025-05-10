#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>

#include "cd.h"
#include "pwd.h"

#define MAX_CMD_LEN 1024

typedef struct {
    const char *name;
    int (*func)(char **args);
} cmds;

cmds *command_list[] = {
    &(cmds){ "cd", run_cd },
    &(cmds){ "pwd", run_pwd },
    NULL
};

typedef enum { CMD_NORMAL, CMD_AND, CMD_OR, CMD_BG } OpType;

typedef struct {
    char* cmd;
    OpType op_type;
    struct op* next;
} op;

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

int execute_subcmd(char *subcmd, int is_bg) {
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
            exit(1);
        } else if (pid > 0) {
            if (is_bg) return 0;
            int status;
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
        } else {
            perror("fork");
            return 1;
        }
    }

    if (is_bg){
        pid_t pid_bg = fork();
        if (pid_bg > 0) return 0;
        else if (pid_bg == -1) {
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

            int ret_val;
            if (run_builtin(args, &ret_val) == 0) exit(1);
            execvp(args[0], args);
            perror("execvp");
            exit(1);
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
    op *head = NULL;
    op *tail = NULL;
    int len = strlen(cmd);
    int ret_val = 0;

    for (int i = 0; i < len;){
        int j = i;
        OpType type = CMD_NORMAL;
        
        while (j < len){
            if (j + 1 < len && cmd[j] == '&' && cmd[j + 1] == '&') {
                type = CMD_AND;
                break;
            }
            if (j + 1 < len && cmd[j] == '|' && cmd[j + 1] == '|') {
                type = CMD_OR;
                break;
            }
            if (cmd[j] == '&') {
                type = CMD_BG;
                break;
            }
            j++;
        }
        int cmd_len = j - i;
        while (cmd_len > 0 && cmd[i + cmd_len - 1] == ' ') cmd_len--;
        while (cmd_len > 0 && cmd[i] == ' ') { i++; cmd_len--; }

        char *cm = strndup(cmd + i, cmd_len);
        op *node = malloc(sizeof(op));

        node->cmd = cm;
        node->op_type = type;
        node->next = NULL;

        if (tail) tail->next = node;
        else head = node;
        tail = node;

        if (type == CMD_AND || type == CMD_OR) i = j + 2;
        else if (type == CMD_BG) i = j + 1;
        else break;
    }

    int keep = 1;
    for (op* iter = head; iter != NULL; iter = iter->next){
        if (keep) ret_val = execute_subcmd(iter->cmd, iter->op_type==CMD_BG);
        if (keep && iter->op_type == CMD_AND && ret_val != 0) keep = 0;
        else if (keep &&iter->op_type == CMD_OR && ret_val == 0) keep = 0;

        free(iter->cmd);
        free(iter);
    }
}

void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main() {
    signal(SIGCHLD, sigchld_handler);
    char line[MAX_CMD_LEN];

    while (1) {
        print_directory();
        memset(line, 0, sizeof(line));
        if (!fgets(line, sizeof(line) - 1, stdin)) break;
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "exit", 4) == 0) break;
        char *saveptr;
        char *cmd = strtok_r(line, ";", &saveptr);
        while (cmd) {
            parse_cmd(cmd);
            cmd = strtok_r(NULL, ";", &saveptr);
        }
    }
    return 0;
}