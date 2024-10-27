#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define PROMPT "PUCITshell:- "

int execute(char* cmd[]);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*);

int main() {
   char *cmdline;
   char** cmd;
   char* prompt = PROMPT;   
   while((cmdline = read_cmd(prompt, stdin)) != NULL) {
      if((cmd = tokenize(cmdline)) != NULL) {
         execute(cmd);
         for(int j = 0; j < MAXARGS+1; j++) free(cmd[j]);
         free(cmd);
         free(cmdline);
      }
   }
   printf("\n");
   return 0;
}

int execute(char* cmd[]) {
    int num_cmds = 0;
    char* command[MAXARGS][MAXARGS];  // Array to hold individual commands
    int i, j = 0, k = 0;
    int in = -1, out = -1;

    // Split the command line into separate commands around pipes
    for (i = 0; cmd[i] != NULL; i++) {
        if (strcmp(cmd[i], "<") == 0) {  // Input redirection
            in = open(cmd[i + 1], O_RDONLY);
            if (in < 0) {
                perror("Failed to open input file");
                return -1;
            }
            cmd[i] = NULL;  // Remove redirection symbol
            i++;
        }
        else if (strcmp(cmd[i], ">") == 0) {  // Output redirection
            out = open(cmd[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) {
                perror("Failed to open output file");
                return -1;
            }
            cmd[i] = NULL;  // Remove redirection symbol
            i++;
        }
        else if (strcmp(cmd[i], "|") == 0) {  // Pipe
            command[num_cmds][j] = NULL;  // End of command
            num_cmds++;
            j = 0;
        } else {
            command[num_cmds][j++] = cmd[i];
        }
    }
    command[num_cmds][j] = NULL;  // Last command
    num_cmds++;

    // Set up pipes for each command
    int pipefd[2 * (num_cmds - 1)];
    for (i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefd + i * 2) == -1) {
            perror("Pipe failed");
            exit(1);
        }
    }

    // Execute each command in the pipeline
    int pid;
    for (i = 0; i < num_cmds; i++) {
        pid = fork();
        if (pid == 0) {  // Child process
            // Set up input redirection for the first command
            if (i == 0 && in != -1) {
                dup2(in, STDIN_FILENO);
                close(in);
            }
            // Set up output redirection for the last command
            if (i == num_cmds - 1 && out != -1) {
                dup2(out, STDOUT_FILENO);
                close(out);
            }

            // Set up input from previous command's pipe, if applicable
            if (i > 0) {
                dup2(pipefd[(i - 1) * 2], STDIN_FILENO);
            }
            // Set up output to next command's pipe, if applicable
            if (i < num_cmds - 1) {
                dup2(pipefd[i * 2 + 1], STDOUT_FILENO);
            }

            // Close all pipe file descriptors in the child
            for (j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefd[j]);
            }

            // Execute the command
            execvp(command[i][0], command[i]);
            perror("Execution failed");
            exit(1);
        } else if (pid < 0) {
            perror("Fork failed");
            return -1;
        }
    }

    // Close all pipe file descriptors in the parent
    for (i = 0; i < 2 * (num_cmds - 1); i++) {
        close(pipefd[i]);
    }

    // Wait for all child processes to complete
    for (i = 0; i < num_cmds; i++) {
        wait(NULL);
    }

    return 0;
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
    while((c = getc(fp)) != EOF) {
        if(c == '\n') break;
        cmdline[pos++] = c;
    }
    if(c == EOF && pos == 0) return NULL;
    cmdline[pos] = '\0';
    return cmdline;
}
