#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>

#include "linenoise/linenoise.h"
#include "linenoise/encodings/utf8.h"

#include "config.h"

void shell_mainloop();
int parse_builtin(int argc, char *const argv[]);
int spawnwait(char *const argv[]);
void dtmsplit(char *str, char *delim, char ***array, int *length);
void buildhints();
void buildcommands();
int startswith(const char *str, const char *prefix);
char *hints(const char *buf, int *color, int *bold);

/* "environment" variables */
char *ps1;
char *username;
char *hostname;
char *curdir;
char *homedir;

char **commands = NULL;
char **files = NULL;

int main(int argc, char **argv) {
    /* fetch prompt */
    if ((ps1 = getenv("PS1")) == NULL) {
        ps1 = malloc(sizeof(char) * (strlen(DEFAULTPROMPT) + 1));
        strcpy(ps1, DEFAULTPROMPT);
    }

    /* fetch "environment" variables */
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

    /* go to home directory */
    chdir(curdir);

    /* init UTF-8 support */
    linenoiseSetEncodingFunctions(linenoiseUtf8PrevCharLen, linenoiseUtf8NextCharLen, linenoiseUtf8ReadCode);

    /* load history if HOME was found */
    linenoiseHistorySetMaxLen(HISTSIZE);
    if (strcmp(homedir, "/"))
        linenoiseHistoryLoad(".cbsh_history");
    else
        fprintf(stderr, "warning: could not fetch home directory, disabling history.\n");

    /* init tab complete & hints */
    buildhints();
    buildcommands();
    linenoiseSetHintsCallback(hints);

    /* run the shell's mainloop */
    shell_mainloop();

    /* save history file */
    chdir(homedir);
    if (strcmp(homedir, "/"))
        linenoiseHistorySave(".cbsh_history");

    printf("bye!\n");
    return 0;
}

/**
 * cbsh's mainloop
 * parses, runs, etc. until exit is called
**/
void shell_mainloop() {
    int running = 1;

    char *command = NULL, *histcmd = NULL;
    size_t maxprompt = strlen(DEFAULTPROMPT) + strlen(username) + strlen(hostname) + MAXCURDIRLEN;
    char *prompt = malloc(sizeof(char) * maxprompt);
    int i;

    while (running) {
        /* print promt & read command (liblinenoise approach) */
        snprintf(prompt, maxprompt, ps1, username, hostname, curdir);
        command = linenoise(prompt);
        histcmd = strdup(command);

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
        int add_to_history = 1;
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
                add_to_history = 0;
                break;
        }

        /* add command to history */
        if (add_to_history)
            linenoiseHistoryAdd(histcmd);

#ifdef DEBUG_OUTPUT
        printf("program exited with exit code %d\n", exit_code);
#endif

        /* free stuff that is no longer used */
        free(command);
        free(histcmd);
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
            buildhints();
            strcpy(curdir, homedir);
            return 0x0;
        } else if (argc == 2) {
            chdir(argv[1]);
            buildhints();
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
void dtmsplit(char *str, char *delim, char ***array, int *length) {
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

/* function to build the hints array */
void buildhints() {
    struct dirent *dent;
    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("opendir");
    }

    /* prevent memory leak when chdir'ing a lot */
    if (files != NULL) {
        int fileidx = 0;
        while (files[fileidx] != NULL) {
            free(files[fileidx++]);
        }
        free(files);
    }

    int alloc_current = 128, alloc_step = 64;
    files = malloc(sizeof(char *) * alloc_current);

    int dent_i = 0;
    while ((dent = readdir(dir)) != NULL) {
        files[dent_i++] = strdup(dent->d_name);
        if (dent_i > alloc_current) {
            alloc_current += alloc_step;
            files = realloc(files, sizeof(char *) * alloc_current);
        }
    }
    files[dent_i] = NULL;

    closedir(dir);
}

/* function to build the commands array */
void buildcommands() {
    char *pathent = getenv("PATH");
    pathent = strdup(pathent); /* this fixes a bug where we would overwrite PATH in the environment */
    if (!pathent) {
        pathent = malloc(sizeof(char) * 14);
        strcpy(pathent, "/usr/bin:/bin");
    }

    /* get array of dirs in PATH */
    char **pathdirs = NULL;
    int count = 0;
    dtmsplit(pathent, ":", &pathdirs, &count);
    pathdirs[count] = NULL;

    /* higher alloc step because PATH will probably contain a lot more files than your average directory */
    int alloc_current = 256, alloc_step = 128, alloc_total = 0;
    commands = malloc(sizeof(char *) * alloc_current);

    /* iterate though every dir in path */
    int pathidx = 0;
    while (pathdirs[pathidx] != NULL) {
        struct dirent *dent;
        DIR *dir = opendir(pathdirs[pathidx]);
        if (dir == NULL) {
            perror("opendir");
            fprintf(stderr, "\npath: %s\n", pathdirs[pathidx]);
        }

        while ((dent = readdir(dir)) != NULL) {
            commands[alloc_total++] = strdup(dent->d_name);
            if (alloc_total > alloc_current) {
                alloc_current += alloc_step;
                commands = realloc(commands, sizeof(char *) * alloc_current);
            }
            /* only read first 32768 files (keep mem footprint little) */
            if (alloc_total > 32768) {
                fprintf(stderr, "WARN: too big alloc because too many files in PATH\n");
                return;
            }
        }

        closedir(dir);
        pathidx++;
    }
    commands[alloc_total] = NULL;
}

/* check if str starts with prefix */
int startswith(const char *str, const char *prefix) {
    return strncmp(prefix, str, strlen(prefix)) == 0;
}

/* hints */
char *hints(const char *buf, int *color, int *bold) {
    /* finds the last element of buf, delimited by spaces */
    char *lastbuf = strdup(buf), *lastarg = lastbuf;
    int bufidx = 0;
    while ((lastbuf = strcasestr(lastbuf, " ")) != NULL) {
        lastarg = ++lastbuf;
        bufidx++;
    }

    if (lastarg[0] == '\0')
        return NULL;

    /* if we're in the first argument of a command, also autocomplete from the list of commands in PATH */
    if (bufidx == 0) {
        int cmdidx = 0;
        while (commands[cmdidx] != NULL) {
            if (startswith(commands[cmdidx], lastarg)) {
                *color = 32;
                *bold = 0;
                free(lastbuf);
                return (commands[cmdidx] + strlen(lastarg));
            }
            cmdidx++;
        }
    }

    int fileidx = 0;
    while (files[fileidx] != NULL) {
        if (startswith(files[fileidx], lastarg)) {
            *color = 35;
            *bold = 0;
            free(lastbuf);
            return (files[fileidx] + strlen(lastarg));
        }
        fileidx++;
    }
    free(lastbuf);
    return NULL;
}
