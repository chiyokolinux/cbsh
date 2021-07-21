/**
 * cbsh - a simple UNIX shell
 * Copyright (c) 2021 Emily <elishikawa@jagudev.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>

#include "linenoise/linenoise.h"
#include "linenoise/encodings/utf8.h"

#include "config.h"

#define NUM_BUILTINS    15

/* types */
struct command_alias {
    char *alias;
    char *command;
};
struct shell_function {
    char *name;
    char ***commands;
};

/* functions */
int shell_mainloop();
int parse_builtin(int argc, char *const argv[]);
int spawnwait(char *const argv[]);
void dtmsplit(char *str, char *delim, char ***array, int *length);
void dtmparse(char *str, char ***array, int *length);
void buildhints(const char *targetdir);
void buildcommands();
int startswith(const char *str, const char *prefix);
int haschar(const char *haystack, const char needle);
int countchar(const char *haystack, const char needle);
char *hints(const char *buf, int *color, int *bold);
void completion(const char *buf, linenoiseCompletions *lc);
int panic(const char *error, const char *details);

/* "environment" variables */
char *ps1;
char *username;
char *hostname;
char *curdir;
char *homedir;

/* autocomplete globals */
char **commands = NULL;
char **files = NULL;
struct command_alias **aliases = NULL;
struct shell_function **functions = NULL;
unsigned int alias_c = 0, function_c = 0;

/**
 * flags that control cbsh's behaviour
 * example length is 16bit, but we can
 * expand to 32bit if needed.
 *
 * |- [reserved for future use]
 * ||- [reserved for future use]
 * |||- [reserved for future use]
 * ||||- [reserved for future use]
 * |||| |- [reserved for future use]
 * |||| ||- [reserved for future use]
 * |||| |||- history disable
 * 0000 0000- multiline mode
**/
unsigned int flags = 0;

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            return panic("unrecognized option", "files are not supported yet.");

        switch (argv[i][1]) {
            case 'm':
                flags |= 1 << 0;
                break;
            case 'H':
                flags |= 1 << 1;
                break;
            case 'v':
                printf("cbsh - version 0.3\n");
                return 0;
            default:
                return panic("unrecognized option", argv[i]);
        }
    }


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

    /* go to home directory and set $PWD*/
    chdir(curdir);
    setenv("PWD", curdir, 1);

    /* init UTF-8 support */
    linenoiseSetEncodingFunctions(linenoiseUtf8PrevCharLen, linenoiseUtf8NextCharLen, linenoiseUtf8ReadCode);

    /* multiline support, if requested */
    linenoiseSetMultiLine(flags & 1 << 0);

    /* load history if HOME was found */
    linenoiseHistorySetMaxLen(HISTSIZE);
    if (strcmp(homedir, "/")) {
        if (!(flags & 1 << 1))
            linenoiseHistoryLoad(".cbsh_history");
    } else {
        fprintf(stderr, "warning: could not fetch home directory, disabling history.\n");
    }

    /* init tab complete & hints */
    buildhints(".");
    buildcommands();
    linenoiseSetCompletionCallback(completion);
    linenoiseSetHintsCallback(hints);

    /* init aliases & shell functions */
    aliases = malloc(sizeof(struct command_alias *));
    functions = malloc(sizeof(struct shell_function *));

    /* run the shell's mainloop */
    int shell_return_value = shell_mainloop();

    /* save history file */
    chdir(homedir);
    if (strcmp(homedir, "/") && !(flags & 1 << 1))
        linenoiseHistorySave(".cbsh_history");

    printf("logout\n");
    return shell_return_value;
}

/**
 * cbsh's mainloop
 * parses, runs, etc. until exit is called
**/
int shell_mainloop() {
    int running = 1, parse_next = 1;

    char *command = NULL, *command_token = NULL;
    size_t maxprompt = strlen(DEFAULTPROMPT) + strlen(username) + strlen(hostname) + MAXCURDIRLEN;
    char *prompt = malloc(sizeof(char) * maxprompt);
    int i, parse_pos = 0, parse_pos_max = 0, exit_expect = -1, exit_expect_satisfy = 0;

    // init $?
    char exit_str[20] = { 0 };
    strcpy(exit_str, "0");
    setenv("?", exit_str, 1);

    while (running) {
        /* print promt & read command (liblinenoise approach) */
        snprintf(prompt, maxprompt, ps1, username, hostname, curdir);
        command = linenoise(prompt);
        if (command) {
            linenoiseHistoryAdd(command);
        } else {
            running = 0;
            break;
        }

        parse_next = 1, parse_pos = 0, parse_pos_max = strlen(command) + 1, exit_expect = -1, exit_expect_satisfy = 0;

        while (parse_next) {
            parse_next = !(parse_pos == parse_pos_max);

            /* parse next command */
            command_token = command + parse_pos + (parse_pos != 0) - ((parse_pos == parse_pos_max) * 2);
            for (; parse_pos < parse_pos_max; parse_pos++) {
                if (command[parse_pos] == ';') {
                    command[parse_pos] = '\0';
                    exit_expect = -1;
                } else if (parse_pos == 0) {
                    continue;
                } else if (command[parse_pos - 1] == '&' && command[parse_pos] == '&') {
                    command[parse_pos - 1] = '\0';
                    exit_expect = 0;
                } else if (command[parse_pos - 1] == '|' && command[parse_pos] == '|') {
                    command[parse_pos - 1] = '\0';
                    exit_expect = 1;
                } else {
                    continue;
                }

                if (exit_expect_satisfy & (1 << 1)) {
                    command_token = command + parse_pos + (parse_pos != 0)  - ((parse_pos == parse_pos_max) * 2);
                    exit_expect_satisfy = 0;
                } else {
                    break;
                }
            }

            /* remove leading spaces */
            while (command_token[0] == ' ') {
                command_token++;
            }

            /* allow semicolon at end of last command */
            if (command_token[0] == '\0')
                break;

            /* read command to arg list */
            char **cmd_argv = NULL;
            int count = 0;
            dtmparse(command_token, &cmd_argv, &count);

            if (count == 0) {
                break;
            }

            cmd_argv[count] = NULL;

            /* exclamation mark shorthands */
            if (cmd_argv[0][0] == '!') {
                panic("not implemented", "linenoise, the line editing library used by cbsh, doesn't allow the program to read the history. thus, implementing exclamation mark shorthands is not possible.\n");
                break;
            }

            /* find possible alias */
            unsigned int aliascheck;
            for (aliascheck = 0; aliascheck < alias_c; aliascheck++) {
                if (!strcmp(cmd_argv[0], aliases[aliascheck]->alias)) {
                    char **cmd_argv_new = NULL;
                    int count_new = 0, offset = 0;

                    dtmparse(strdup(aliases[aliascheck]->command), &cmd_argv_new, &count_new);
                    cmd_argv_new = realloc(cmd_argv_new, sizeof(char *) * (count_new + count + 1));

                    for (; offset <= count; offset++) {
                        cmd_argv_new[count_new + offset] = cmd_argv[offset + 1];
                    }
                    count = count_new + count - 1;

                    /* avoid self-binding problems */
                    if (!strcmp(cmd_argv[0], cmd_argv_new[0])) {
                        cmd_argv = cmd_argv_new;
                        break;
                    } else {
                        cmd_argv = cmd_argv_new;
                        aliascheck = -1;
                    }
                }
            }

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
                case 0x1:
                case 0x0:
                    break;
                case 0xAA:
                    fprintf(stderr, "%s: wrong number of arguments!\n", cmd_argv[0]);
                    break;
                default:
                    if ((exit_code & 0xFFFF) == 0xDEAD) {
                        return (exit_code >> 16);
                    } else {
                        fprintf(stderr, "error: parse_builtin returned an unknown action identifier (%hd)\n", exit_code);
                    }
                    break;
            }

            // put exit code into env
            snprintf(exit_str, 19, "%d", exit_code);
            setenv("?", exit_str, 1);

            if (exit_expect == 0 && exit_code != 0)
                exit_expect_satisfy = 2;
            else if (exit_expect == 1 && exit_code == 0)
                exit_expect_satisfy = 3;
            else
                exit_expect_satisfy = 0;

#ifdef DEBUG_OUTPUT
            printf("program exited with exit code %d\n", exit_code);
#endif

            /* if a command created a file, take note of that */
            buildhints(".");

            /* free stuff */
            free(cmd_argv);
        }

        /* free stuff that is no longer used */
        free(command);
    }

    return 0;
}

/**
 * parses a builtin function
 * returns 0x1337 if no builtin function
 * with the specified name was found
 * returns 0xDEAD for exit
 * returns 0x0 or 0x1 to specify success or failure
 * returns 0xAA if command usage is wrong
 * returns 0xBA to shift args and re-parse
**/
int parse_builtin(int argc, char *const argv[]) {
    if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "logout")) {
        if (argc == 1) {
            return 0xDEAD;
        } else if (argc == 2) {
            return 0xDEAD | (atoi(argv[1]) << 16);
        }
        return 0xAA;
    } else if (!strcmp(argv[0], "cd") || !strcmp(argv[0], "chdir")) {
        if (argc == 1) {
            chdir(homedir);
            strcpy(curdir, homedir);
            setenv("PWD", curdir, 1);
            return 0x0;
        } else if (argc == 2) {
            if (chdir(argv[1])) {
                perror("chdir");
                return 0x1;
            }
            getcwd(curdir, MAXCURDIRLEN);
            setenv("PWD", curdir, 1);
            return 0x0;
        }
        return 0xAA;
    } else if (!strcmp(argv[0], "export") || !strcmp(argv[0], "setenv")) {
        if (argc == 1) {
            return 0xAA;
        }

        int varidx;
        for (varidx = 1; varidx < argc; varidx++) {
            char *key = malloc(sizeof(char) * 64), *value = malloc(sizeof(char) * 1024);
            if (sscanf(argv[varidx], "%63[^=]=%1023[^\n]", key, value) == 2) {
                setenv(key, value, 1);
                free(key);
                free(value);
            } else {
                free(key);
                free(value);
                return 0xAA;
            }
        }
        return 0x0;
    } else if (haschar(argv[0], '=')) {
        char *key = malloc(sizeof(char) * 64), *value = malloc(sizeof(char) * 1024);
        if (sscanf(argv[0], "%63[^=]=%1023[^\n]", key, value) == 2) {
            setenv(key, value, 1);
            free(key);
            free(value);

            /* if there were arguments left, run the command after all var declarations */
            if (argc == 1) {
                return 0x0;
            } else {
                return 0xBA;
            }
        } else {
            free(key);
            free(value);
        }
        return 0xAA;
    } else if (!strcmp(argv[0], "getenv")) {
        if (argc == 2) {
            char *envvar = getenv(argv[1]);
            if (envvar) {
                printf("%s\n", envvar);
                return 0x0;
            } else {
                printf("error: getenv: no such variable\n");
                return 0x1;
            }
        }
        return 0xAA;
    } else if (!strcmp(argv[0], "builtin")) {
        if (argc >= 2) {
            return parse_builtin(argc - 1, argv + 1);
        }
        return 0xAA;
    } else if (!strcmp(argv[0], "command")) {
        if (argc == 1) {
            return 0xAA;
        }

        int option;
        char *pathent, *pathold;
        while ((option = getopt(argc, argv, "pVv")) != -1) {
            switch (option) {
                case 'p':
                    if (argc == 1) {
                        return 0xAA;
                    }

                    pathent = strdup("PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");
                    pathold = strdup(getenv("PATH"));
                    putenv(pathent);
                    spawnwait(argv + 2);
                    setenv("PATH", pathold, 1);
                    free(pathent);
                    free(pathold);
                    return 0x0;
                case 'v':
                case 'V':
                case '?':
                    return 0xAA;
            }
        }

        spawnwait(argv + 1);
        return 0x0;
    } else if (!strcmp(argv[0], "echo")) {
        int putnewline = 1, current = 1;

        if (argc > 1) {
            if (!strcmp(argv[1], "-e")) {
                putnewline = 0;
                current++;
            }
        }

        for (; current < argc; current++) {
            int argl = strlen(argv[current]), cchar = 0;
            for (; cchar < argl; cchar++) {
                putchar(argv[current][cchar]);
            }

            if (current != argc - 1) {
                putchar(' ');
            }
        }
        if (putnewline) {
            putchar('\n');
        }

        return 0x0;
    } else if (!strcmp(argv[0], ":")) {
        return 0x0;
    } else if (!strcmp(argv[0], ".") || !strcmp(argv[0], "source")) {
        return 0x0;
    } else if (!strcmp(argv[0], "alias")) {
        if (argc == 1) {
            unsigned int i;

            for (i = 0; aliases[i] != NULL; i++) {
                printf("alias %s='%s'\n", aliases[i]->alias, aliases[i]->command);
            }

            return 0x0;
        }

        /* fix memory */
        aliases = realloc(aliases, sizeof(struct command_alias *) * (alias_c + argc));

        int varidx;
        for (varidx = 1; varidx < argc; varidx++) {
            char *key = malloc(sizeof(char) * 128), *value = malloc(sizeof(char) * 2048);
            if (sscanf(argv[varidx], "%127[^=]=%2047[^\n]", key, value) == 2) {
                aliases[alias_c] = malloc(sizeof(struct command_alias));

                aliases[alias_c]->alias = key;
                aliases[alias_c]->command = value;

                alias_c++;
            } else {
                free(key);
                free(value);
                return 0xAA;
            }
        }

        /* add alias to command list */
        buildcommands();

        return 0x0;
    } else if (!strcmp(argv[0], "unalias")) {
        return 0x0;
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
            signal(SIGINT, SIG_IGN);
            waitpid(chpid, &waitstatus, WUNTRACED);
            signal(SIGINT, SIG_DFL);
            return WEXITSTATUS(waitstatus);
    }
}

/**
 * splits str at delim into array with length elements
**/
void dtmsplit(char *str, char *delim, char ***array, int *length) {
    int i = 0;
    char *token;
    char **res = malloc(sizeof(char *) * 2);

    /* get the first token */
    token = strtok(str, delim);
    while (token != NULL) {
        res = (char **) realloc(res, (i + 2) * sizeof(char *));
        res[i] = token;
        token = strtok(NULL, delim);
        i++;
    }

    *array = res;
    *length = i;
}

/**
 * parses str with shell syntax
 *
 * NEW PARSER! now uses a char-by-char
 * seek-ahead parser because, while an
 * action-on-token back-looking parser
 * may be faster, it is significantly
 * more difficult to write and harder
 * to read.
**/
void dtmparse(char *str, char ***array, int *length) {
    int i = 0, i_alloc = 2, in_quotes = 0, maxlen = strlen(str), k = 0, helper = 0, str_alloc = maxlen + 1, str_alloc_step = 32, str_pos = 0;
    int *res = malloc(sizeof(int) * 3);
    char *str_new = malloc(sizeof(char) * (str_alloc));

    /* first pass: parse with relative offsets */
    res[i] = str_pos;
    for (; k <= maxlen; k++) {
        switch (str[k]) {
            case '$':
                if (str[k + 1] == '(') {
                    // TODO
                } else if (str[k + 1] == '"' || str[k + 1] == '\'') {
                    break;
                } else if (str[k + 1] == '{') {
                    /* seek to closing bracket */
                    for (helper = k; str[helper] != '}' && helper <= maxlen; helper++);

                    /* check syntax validity */
                    if (str[helper] != '}') {
                        panic("syntax error", "unclosed curly braces found\n");
                        free(res);
                        free(str_new);
                        *array = NULL;
                        *length = 0;
                        return;
                    }

                    /* terminate var name string */
                    str[helper] = '\0';

                    /* we null-terminated the segment, so this is fine */
                    char *envvar = getenv(str + k + 2);

                    k = helper;

                    if (envvar) {
                        int lenvvar = strlen(envvar); // haha funny pun
                        /* make sure we have enough bytes */
                        if (str_pos + lenvvar >= str_alloc - 3) {
                            str_new = realloc(str_new, sizeof(char) * (str_alloc + lenvvar + 2));
                            str_alloc = str_alloc + lenvvar + 2;
                        }

                        /* simple strcpy */
                        for (helper = 0; helper < lenvvar; helper++) {
                            str_new[str_pos++] = envvar[helper];
                        }
                    } else {
#ifdef DEBUG_OUTPUT
                        panic("getenv", "variable not found in environment\n");
#endif
                        break;
                    }
                } else {
                    /* seek to terminating special char */
                    for (helper = k + 1; helper < maxlen &&
                             str[helper] != '}' &&
                             str[helper] != '"' &&
                             str[helper] != '\'' &&
                             str[helper] != '$' &&
                             str[helper] != ' ' &&
                             str[helper] != '\\' &&
                             str[helper] != '='; helper++);

                    /* terminate var name string */
                    char tmp = str[helper];
                    str[helper] = '\0';

                    /* we null-terminated the segment, so this is fine */
                    char *envvar = getenv(str + k + 1);

                    k = helper - 1;
                    str[helper] = tmp;

                    if (envvar) {
                        int lenvvar = strlen(envvar); // haha funny pun
                        /* make sure we have enough bytes */
                        if (str_pos + lenvvar >= str_alloc - 3) {
                            str_new = realloc(str_new, sizeof(char) * (str_alloc + lenvvar + 2));
                            str_alloc = str_alloc + lenvvar + 2;
                        }

                        /* simple strcpy */
                        for (helper = 0; helper < lenvvar; helper++) {
                            str_new[str_pos++] = envvar[helper];
                        }
                    } else {
#ifdef DEBUG_OUTPUT
                        panic("getenv", "variable not found in environment\n");
#endif
                        break;
                    }
                }
                break;
            case ' ':
                /* make sure we have enough bytes */
                if (str_pos >= str_alloc - 2) {
                    str_new = realloc(str_new, sizeof(char) * (str_alloc + str_alloc_step));
                    str_alloc = str_alloc + str_alloc_step;
                }

                if (!in_quotes) {
                    /* don't parse empty arguments */
                    if (str_new[str_pos - 1] != '\0') {
                        str_new[str_pos++] = '\0';
                        res[++i] = str_pos;
                    }
                } else {
                    str_new[str_pos++] = ' ';
                }
                break;
            case '\'':
                if (in_quotes == 1) {
                    /* make sure we have enough bytes */
                    if (str_pos >= str_alloc - 2) {
                        str_new = realloc(str_new, sizeof(char) * (str_alloc + str_alloc_step));
                        str_alloc = str_alloc + str_alloc_step;
                    }
                    /* just copy the char */
                    str_new[str_pos++] = str[k];
                } else if (in_quotes == 2) {
                    in_quotes = 0;
                } else {
                    in_quotes = 2;
                }
                break;
            case '"':
                if (in_quotes == 2) {
                    /* make sure we have enough bytes */
                    if (str_pos >= str_alloc - 2) {
                        str_new = realloc(str_new, sizeof(char) * (str_alloc + str_alloc_step));
                        str_alloc = str_alloc + str_alloc_step;
                    }
                    /* just copy the char */
                    str_new[str_pos++] = str[k];
                } else if (in_quotes == 1) {
                    in_quotes = 0;
                } else {
                    in_quotes = 1;
                }
                break;
            case '\\':
                /* ignore this char and skip to the next one */
                k++;
                __attribute__ ((fallthrough));
            default:
                /* make sure we have enough bytes */
                if (str_pos >= str_alloc - 2) {
                    str_new = realloc(str_new, sizeof(char) * (str_alloc + str_alloc_step));
                    str_alloc = str_alloc + str_alloc_step;
                }
                /* just copy the char if it has no special meaning */
                str_new[str_pos++] = str[k];
                break;
        }
        if (i >= i_alloc) {
            res = realloc(res, sizeof(int) * (i + 3));
            i_alloc += 2;
        }
    }

    /* fix: skip empty args at end */
    if (str_new[str_pos - 2] == '\0') {
        i--;
    }

    /* check for quote termination */
    if (in_quotes) {
        panic("syntax error", "unterminated quote found\n");
        free(res);
        free(str_new);
        *array = NULL;
        *length = 0;
        return;
    }

    /* second pass: convert relative offsets to actual addresses
                    because realloc moves memory */
    char **res_final = malloc(sizeof(char *) * (i + 2));
    for (k = 0; k <= i; k++) {
        res_final[k] = str_new + res[k];
    }
    free(res);

    *array = res_final;
    *length = i + 1;
}

/* function to build the hints array */
void buildhints(char const *targetdir) {
    struct dirent *dent;
    DIR *dir = opendir(targetdir);
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
        files[dent_i] = malloc(sizeof(char) * (strlen(dent->d_name) + 2 + countchar(dent->d_name, ' ')));

        /* strcpy with escaping, yay */
        int pos, outpos = 0;
        for (pos = 0; dent->d_name[pos] != '\0'; pos++, outpos++) {
            /* maybe this gets optimized? need to check perf later. */
            switch (dent->d_name[pos]) {
                case ' ':
                    files[dent_i][outpos++] = '\\';
                    __attribute__ ((fallthrough));
                default:
                    files[dent_i][outpos] = dent->d_name[pos];
            }
        }
        files[dent_i][outpos] = '\0';

        dent_i++;

        if (dent_i >= alloc_current) {
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
            if (alloc_total >= alloc_current) {
                alloc_current += alloc_step;
                commands = realloc(commands, sizeof(char *) * alloc_current);
            }
            /* only read first 32768 files (keep mem footprint small) */
            if (alloc_total > 32768) {
                fprintf(stderr, "WARN: too big alloc because too many files in PATH\n");
                return;
            }
        }

        closedir(dir);
        pathidx++;
    }

    /* add builtins */
    if (alloc_total + NUM_BUILTINS > alloc_current) {
        alloc_current += NUM_BUILTINS;
        commands = realloc(commands, sizeof(char *) * alloc_current);
    }
    commands[alloc_total++] = "cd";
    commands[alloc_total++] = "chdir";
    commands[alloc_total++] = "exit";
    commands[alloc_total++] = "export";
    commands[alloc_total++] = "setenv";
    commands[alloc_total++] = "getenv";
    commands[alloc_total++] = "builtin";
    commands[alloc_total++] = "command";
    commands[alloc_total++] = "echo";
    commands[alloc_total++] = "logout";
    commands[alloc_total++] = ":";
    commands[alloc_total++] = ".";
    commands[alloc_total++] = "source";
    commands[alloc_total++] = "alias";
    commands[alloc_total++] = "unalias";

    /* add aliases */
    if (alloc_total + alias_c + function_c > (unsigned)alloc_current) {
        alloc_current += (alias_c + function_c);
        commands = realloc(commands, sizeof(char *) * alloc_current);
    }
    unsigned int idx;
    for (idx = 0; idx < alias_c; idx++) {
        commands[alloc_total++] = aliases[idx]->alias;
    }
    for (idx = 0; idx < function_c; idx++) {
        commands[alloc_total++] = functions[idx]->name;
    }


    /* correctly terminate array */
    commands[alloc_total] = NULL;
}

/* check if str starts with prefix */
int startswith(const char *str, const char *prefix) {
    return strncmp(prefix, str, strlen(prefix)) == 0;
}

/* returns first position (staring with 1) of needle
   in haystack if haystack contains needle.
   if not, returns 0. */
int haschar(const char *haystack, const char needle) {
    unsigned int position;

    for (position = 0; haystack[position] != '\0'; position++) {
        if (haystack[position] == needle) {
            return ++position;
        }
    }

    return 0;
}

/* like haschar, but does not return after first match and returns
   how many occurrences of needle are haystack. */
int countchar(const char *haystack, const char needle) {
    unsigned int position, count = 0;

    for (position = 0; haystack[position] != '\0'; position++) {
        if (haystack[position] == needle) {
            count++;
        }
    }

    return count;
}

/* hints */
char *hints(const char *buf, int *color, int *bold) {
    /* finds the last element of buf, delimited by spaces */
    char *lastbuf = strdup(buf), *lastarg = lastbuf;
    int bufidx = 0;

    while (lastarg[0] == ' ') {
        lastarg = ++lastbuf;
    }

    while ((lastbuf = strstr(lastbuf, " ")) != NULL) {
        lastarg = ++lastbuf;
        bufidx++;
        if (strlen(lastarg) < 2)
            continue;
        if (!strncmp(lastarg, "&& ", 3) || !strncmp(lastarg, "|| ", 3)) {
            bufidx = -1;
        } else if (lastarg[0] == ';') {
            bufidx = -(lastarg[1] == ' ');
            lastarg++;
        } else if (*(lastarg - 2) == ';') {
            bufidx = 0;
        }
    }

    if (lastarg[0] == '\0') {
        free(lastbuf);
        return NULL;
    }

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

/* tab auto-complete */
void completion(const char *buf, linenoiseCompletions *lc) {
    /* finds the last element of buf, delimited by spaces */
    char *firstbuf = strdup(buf), *lastbuf = firstbuf, *lastarg = firstbuf;
    int bufidx = 0;

    while (lastarg[0] == ' ') {
        lastarg = ++lastbuf;
    }

    while ((lastbuf = strstr(lastbuf, " ")) != NULL) {
        lastarg = ++lastbuf;
        bufidx++;
        if (strlen(lastarg) < 2)
            continue;
        if (!strncmp(lastarg, "&& ", 3) || !strncmp(lastarg, "|| ", 3)) {
            bufidx = -1;
        } else if (lastarg[0] == ';') {
            bufidx = -(lastarg[1] == ' ');
            lastarg++;
        } else if (*(lastarg - 2) == ';') {
            bufidx = 0;
        }
    }

    if (lastarg[0] == '\0') {
        free(firstbuf);
        return;
    }

    /* if we're in the first argument of a command, also autocomplete from the list of commands in PATH */
    if (bufidx == 0) {
        int cmdidx = 0;
        while (commands[cmdidx] != NULL) {
            if (startswith(commands[cmdidx], lastarg)) {
                char *tmp = malloc(sizeof(char) * (strlen(firstbuf) + strlen(commands[cmdidx]) - strlen(lastarg) + 1));
                strcpy(tmp, firstbuf);
                strcat(tmp, commands[cmdidx] + strlen(lastarg));
                linenoiseAddCompletion(lc, tmp);
                free(tmp);
            }
            cmdidx++;
        }
    }

    int fileidx = 0;
    while (files[fileidx] != NULL) {
        if (startswith(files[fileidx], lastarg)) {
            char *tmp = malloc(sizeof(char) * (strlen(firstbuf) + strlen(files[fileidx]) - strlen(lastarg) + 1));
            strcpy(tmp, firstbuf);
            strcat(tmp, files[fileidx] + strlen(lastarg));
            linenoiseAddCompletion(lc, tmp);
            free(tmp);
        }
        fileidx++;
    }
    free(firstbuf);
}

/* print error msg and return non-zero exit value */
int panic(const char *error, const char *details) {
    fprintf(stderr, "\ncbsh: error: %s\n", error);
    if (details != NULL)
        fprintf(stderr, "   %s\n", details);
    return -1;
}
