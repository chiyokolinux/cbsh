#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "config.h"

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

void shell_mainloop();
int parse_builtin(int argc, char *const argv[]);
int spawnwait(char *const argv[]);
void dtmsplit(char *str, const char *delim, char ***array, int *length);

/* "environment" variables */
char *ps1;
char *username;
char *hostname;
char *curdir;
char *homedir;

int main(int argc, char **argv) {
    if ((ps1 = getenv("PS1")) == NULL) {
        ps1 = malloc(sizeof(char) * (strlen(DEFAULTPROMPT) + 1));
        strcpy(ps1, DEFAULTPROMPT);
    }

    username = getenv("USER");
    if (!username) {
        username = malloc(sizeof(char) * 6);
        strcpy(username, "emily");
    }
    hostname = getenv("HOSTNAME");
    if (!hostname) {
        hostname = malloc(sizeof(char) * 8);
        strcpy(hostname, "chiyoko");
    }
    curdir = malloc(sizeof(char) * MAXCURDIRLEN);
    strcpy(curdir, getenv("HOME"));
    if (curdir[0] == '\0')
        strcpy(curdir, "/");
    homedir = strdup(curdir);

    chdir(curdir);

    shell_mainloop();

    printf("bye!\n");
    return 0;
}

/**
 * cbsh's mainloop
 * parses, runs, etc. until exit is called
**/
void shell_mainloop() {
    int running = 1;

    char *command = NULL;
#ifdef USE_READLINE
    size_t maxprompt = strlen(DEFAULTPROMPT) + strlen(username) + strlen(hostname) + MAXCURDIRLEN;
#else
    size_t len = 0, maxprompt = strlen(DEFAULTPROMPT) + strlen(username) + strlen(hostname) + MAXCURDIRLEN;
#endif
    char *prompt = malloc(sizeof(char) * maxprompt);
#ifdef USE_READLINE
    int i;
#else
    int read, i;
#endif

    while (running) {
#ifdef USE_READLINE
        snprintf(prompt, maxprompt, ps1, username, hostname, curdir);
        command = readline(prompt);
#else
        /* print promt & read command (libc approach) */
        printf(ps1, username, hostname, curdir);
        read = getline(&command, &len, stdin);

        /* strip newline */
        for (i = read; command[i] != '\n'; i--) ;
        command[i] = '\0';
#endif

#ifdef DEBUG_OUTPUT
        printf("read line: %s\n", command);
#endif

        /* read command to arg list */
        char **cmd_argv = NULL;
        int count = 0;
        dtmsplit(command, " ", &cmd_argv, &count);
        cmd_argv[count] = NULL;

#ifdef DEBUG_OUTPUT
        printf("parsed command: ");
        for (i = 0; i < count; i++) {
            printf("[%s]", cmd_argv[i]);
        }
        printf("\n");
        fflush(stdout);
#endif

        /* run command */
        int exit_code = 0;
        switch ((exit_code = parse_builtin(count, cmd_argv))) {
            case 0x1337:
                exit_code = spawnwait(cmd_argv);
                break;
            case 0xDEAD:
                running = 0;
                break;
            case 0x0:
                break;
            case 0xAA:
                fprintf(stderr, "%s: wrong number of arguments!\n", cmd_argv[0]);
                break;
            default:
                fprintf(stderr, "error: parse_builtin returned an unknown action identifier (%hd)\n", exit_code);
                break;
        }

#ifdef DEBUG_OUTPUT
        printf("program exited with exit code %d\n", exit_code);
#endif
    }
}

/**
 * parses a builtin function
 * returns 0x1337 if no builtin function
 * with the specified name was found
 * returns 0xDEAD for exit
**/
int parse_builtin(int argc, char *const argv[]) {
    if (!strcmp(argv[0], "exit")) {
        if (argc == 1)
            return 0xDEAD;
        return 0xAA;
    } else if (!strcmp(argv[0], "cd") || !strcmp(argv[0], "chdir")) {
        if (argc == 1) {
            chdir(homedir);
            strcpy(curdir, homedir);
            return 0x0;
        } else if (argc == 2) {
            chdir(argv[1]);
            getcwd(curdir, MAXCURDIRLEN);
            return 0x0;
        }
        return 0xAA;
    }
    return 0x1337;
}

/**
 * spawns argv, waits for it to die and
 * then returns its return value
**/
int spawnwait(char *const argv[]) {
    int waitstatus;
    pid_t chpid = fork();
    switch (chpid) {
        case 0:
            execvp(argv[0], argv);
            perror("execvp");
            _exit(1);
        case -1:
            perror("fork");
            return -1;
        default:
            wait(&waitstatus);
            return WEXITSTATUS(waitstatus);
    }
}

/**
 * splits str at delim into array with length elements
**/
void dtmsplit(char *str, const char *delim, char ***array, int *length) {
    int i = 0;
    char *token;
    char **res = (char **) malloc(0 * sizeof(char *));

    /* get the first token */
    token = strtok(str, delim);
    while( token != NULL ) {
        res = (char **) realloc(res, (i + 2) * sizeof(char *));
        res[i] = token;
        i++;
        token = strtok(NULL, delim);
    }
    *array = res;
    *length = i;
}
