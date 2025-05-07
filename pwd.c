#include "pwd.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int run_pwd(char **args) {
    int L = 1;

    if (args[1] != NULL) {
        if (strcmp(args[1], "-P") == 0) {
            L = 0;
        } else if (strcmp(args[1], "-L") != 0) {
            fprintf(stderr, "pwd: invalid option %s\n", args[1]);
            return 1;
        }
    }

    if (L) {
        char *pwd = getenv("PWD");
        if (pwd){
            printf("%s\n", pwd);
            return 0;
        }
        else{
            perror("PWD");
            return 1;
        }
    } else {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL){
            printf("%s\n", cwd);
            return 0;
        }
        else{
            perror("getcwd");
            return 1;
        }
    }
}