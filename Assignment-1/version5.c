#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define PROMPT "PUCITshell:- "
#define MAX_HISTORY 10
#define MAX_JOBS 100

int execute(char* cmd[], char* history[], int* history_count);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*);
void add_to_history(char* cmd, char* history[], int* history_count);
void list_jobs();
void kill_job(int pid);
void display_help();

// Job structure to keep track of background jobs
typedef struct {
    int job_id;
    pid_t pid;
    char command[MAX_LEN];
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;
int job_counter = 1;

// Signal handler to reap background processes
void handle_sigchld(int sig) {
    int saved_errno = errno;
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid <= 0) break;
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                for (int j = i; j < job_count - 1; j++) {
                    jobs[j] = jobs[j + 1];
                }
                job_count--;
                break;
            }
        }
    }
    errno = saved_errno;
}

int main() {
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror("sigaction");
        exit(1);
    }

    char *cmdline;
    char** cmd;
    char* history[MAX_HISTORY] = { NULL };
    int history_count = 0;

    while((cmdline = read_cmd(PROMPT, stdin)) != NULL) {
        if (cmdline[0] == '!') {
            if (strcmp(cmdline, "!-1") == 0) {
                if (history_count > 0) {
                    cmdline = strdup(history[history_count - 1]);
                    printf("Repeating command: %s\n", cmdline);
                } else {
                    printf("No commands in history.\n");
                    continue;
                }
            } else {
                int cmd_num = atoi(&cmdline[1]);
                if (cmd_num >= 1 && cmd_num <= history_count) {
                    cmdline = strdup(history[cmd_num - 1]);
                    printf("Repeating command: %s\n", cmdline);
                } else {
                    printf("Invalid command number\n");
                    continue;
                }
            }
        } else {
            add_to_history(cmdline, history, &history_count);
        }

        if((cmd = tokenize(cmdline)) != NULL) {
            execute(cmd, history, &history_count);
            for(int j = 0; j < MAXARGS + 1; j++) free(cmd[j]);
            free(cmd);
            free(cmdline);
        }
    }
    printf("\n");

    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }
    return 0;
}

void add_to_history(char* cmd, char* history[], int* history_count) {
    if (*history_count < MAX_HISTORY) {
        history[*history_count] = strdup(cmd);
        (*history_count)++;
    } else {
        free(history[0]);
        for (int i = 1; i < MAX_HISTORY; i++) {
            history[i - 1] = history[i];
        }
        history[MAX_HISTORY - 1] = strdup(cmd);
    }
}

int execute(char* cmd[], char* history[], int* history_count) {
    if (strcmp(cmd[0], "cd") == 0) {
        if (cmd[1] != NULL) {
            if (chdir(cmd[1]) != 0) {
                perror("cd failed");
            }
        } else {
            printf("cd: missing argument\n");
        }
        return 1;
    }
    if (strcmp(cmd[0], "exit") == 0) {
        exit(0);
    }
    if (strcmp(cmd[0], "jobs") == 0) {
        list_jobs();
        return 1;
    }
    if (strcmp(cmd[0], "kill") == 0) {
        if (cmd[1] != NULL) {
            int pid = atoi(cmd[1]);
            kill_job(pid);
        } else {
            printf("kill: missing PID argument\n");
        }
        return 1;
    }
    if (strcmp(cmd[0], "help") == 0) {
        display_help();
        return 1;
    }

    int background = 0;
    int in = -1, out = -1;
    int num_cmds = 0;
    char* command[MAXARGS][MAXARGS];
    int i, j = 0;
    pid_t pid;

    for (i = 0; cmd[i] != NULL; i++) {
        if (strcmp(cmd[i], "&") == 0) {
            background = 1;
            cmd[i] = NULL;
            break;
        } else if (strcmp(cmd[i], "<") == 0) {
            in = open(cmd[i + 1], O_RDONLY);
            if (in < 0) {
                perror("Failed to open input file");
                return -1;
            }
            cmd[i] = NULL;
            i++;
        } else if (strcmp(cmd[i], ">") == 0) {
            out = open(cmd[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) {
                perror("Failed to open output file");
                return -1;
            }
            cmd[i] = NULL;
            i++;
        } else if (strcmp(cmd[i], "|") == 0) {
            command[num_cmds][j] = NULL;
            num_cmds++;
            j = 0;
        } else {
            command[num_cmds][j++] = cmd[i];
        }
    }
    command[num_cmds][j] = NULL;
    num_cmds++;

    int pipefd[2 * (num_cmds - 1)];
    for (i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefd + i * 2) == -1) {
            perror("Pipe failed");
            exit(1);
        }
    }

    for (i = 0; i < num_cmds; i++) {
        pid = fork();
        if (pid == 0) {
            if (i == 0 && in != -1) {
                dup2(in, STDIN_FILENO);
                close(in);
            }
            if (i == num_cmds - 1 && out != -1) {
                dup2(out, STDOUT_FILENO);
                close(out);
            }
            if (i > 0) {
                dup2(pipefd[(i - 1) * 2], STDIN_FILENO);
            }
            if (i < num_cmds - 1) {
                dup2(pipefd[i * 2 + 1], STDOUT_FILENO);
            }
            for (j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefd[j]);
            }
            execvp(command[i][0], command[i]);
            perror("Execution failed");
            exit(1);
        } else if (pid < 0) {
            perror("Fork failed");
            return -1;
        }
    }

    for (i = 0; i < 2 * (num_cmds - 1); i++) {
        close(pipefd[i]);
    }

    if (!background) {
        for (i = 0; i < num_cmds; i++) {
            wait(NULL);
        }
    } else {
        jobs[job_count].job_id = job_counter++;
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, cmd[0], MAX_LEN);
        job_count++;
        printf("[%d] %d\n", jobs[job_count - 1].job_id, pid);
    }
    return 0;
}

void list_jobs() {
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %d %s\n", jobs[i].job_id, jobs[i].pid, jobs[i].command);
    }
}

void kill_job(int pid) {
    if (kill(pid, SIGKILL) == 0) {
        printf("Process %d killed successfully\n", pid);
    } else {
        perror("Failed to kill process");
    }
}

void display_help() {
    printf("Built-in commands:\n");
    printf("cd <directory>  : Change the current working directory\n");
    printf("exit            : Exit the shell\n");
    printf("jobs            : List all background jobs\n");
    printf("kill <pid>      : Kill the specified process\n");
    printf("help            : Display this help message\n");
}

char** tokenize(char* cmdline) {
    char** cmd = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for(int j = 0; j < MAXARGS + 1; j++) {
        cmd[j] = (char*)malloc(sizeof(char) * ARGLEN);
        bzero(cmd[j], ARGLEN);
    }
    if(cmdline[0] == '\0') return NULL;

    int argnum = 0;
    char* cp = cmdline;
    char* start;
    int len;

    while(*cp != '\0') {
        while(*cp == ' ' || *cp == '\t') cp++;
        start = cp;
        len = 1;
        while(*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) len++;
        strncpy(cmd[argnum], start, len);
        cmd[argnum][len] = '\0';
        argnum++;
    }
    cmd[argnum] = NULL;
    return cmd;
}

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    int c, pos = 0;
    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);
    if (cmdline == NULL) {
        perror("malloc failed");
        exit(1);
    }

    while((c = getc(fp)) != EOF) {
        if(c == '\n') break;
        if(c == 3) {
            printf("\n");
            free(cmdline);
            return NULL;
        }
        if(pos < MAX_LEN - 1) {
            cmdline[pos++] = c;
        }
    }
    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL;
    }

    cmdline[pos] = '\0';
    return cmdline;
}
