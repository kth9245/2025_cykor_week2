#include "cd.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

int run_cd(char **args) {
    int L = 1;
    char *target = NULL;

    int i = 1;
    while (args[i] && args[i][0] == '-') {
        if (strcmp(args[i], "-L") == 0) {
            L = 1;
        } else if (strcmp(args[i], "-P") == 0) {
            L = 0;
        } else {
            fprintf(stderr, "cd: invalid option %s\n", args[i]);
            return 1;
        }
        i++;
    }

    if (args[i] == NULL) {
        target = getenv("HOME");
    } else if (args[i][0] == '~') {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s%s", getenv("HOME"), args[i] + 1);
        target = path;
    } else {
        target = args[i];
    }

    if (!L) {
        char real_target[PATH_MAX];
        if (realpath(target, real_target) == NULL) {
            perror("cd -P");
            return 1;
        }
        target = real_target;
    }

    if (chdir(target) != 0) {
        perror("cd");
        return 1;
    }

    char new_pwd[PATH_MAX];
    if (getcwd(new_pwd, sizeof(new_pwd)) != NULL) {
        setenv("PWD", new_pwd, 1);
    }
    return 0;
}