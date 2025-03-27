#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <unistd.h>

// Program Name: SMALLSH
// Author: JC
// Description: This program implements features of well-known shells such as bash. These features include: providing a
// prompt for running commands, handling blank lines and comments, executing built-in and other commands, supporting
// input and output redirection, supporting fg and bg processes, and implementing custom handlers for signals.

// MACROS
#define INPUT_LENGTH 2048       // maximum length of characters for command line
#define MAX_ARGS 512            // maximum number of arguments for command line

// Global Variables
int status = 0;                 // tracks the status of the last fg process ran
int fgOnly = 0;                 // foreground (fg) process toggle, zero = False
pid_t bgProcesses[100];         // an array to hold open background (bg) processes
int bgCount = 0;                // tracks number of open bg processes

/* Citation: Used sample code, sample_parser.c provided on assignment page
Date Accessed: 2025-02-26
URL: https://canvas.oregonstate.edu/courses/1987883/assignments/9864854?module_item_id=24956222
Modified for Assignment 4 */
// structure for command line
struct command_line {
    char *argv[MAX_ARGS + 1];   // an array of arguments (args)
    int argc;                   // number of args
    char *input_file;           // input redirection
    char *output_file;          // output redirection
    bool is_bg;                 // tracks if bg process
};

// Function Prototypes
void exitStatus(int status); 
void backgroundChild(struct command_line *curr_command); 
void foregroundChild(struct command_line *curr_command);

/* Citation: Used sample code, sample_parser.c provided on assignment page
Date Accessed: 2025-02-26
URL: https://canvas.oregonstate.edu/courses/1987883/assignments/9864854?module_item_id=24956222
Modified for Assignment 4 */
// structure to parse command entered by user
struct command_line *parse_input() {
    char input[INPUT_LENGTH];   // buffer to store user input
    // allocates memory for curr_command
    struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));

    // prompts user for input, reprompts if comment or empty line is entered
    do {
        // Gets/reads input from user
        printf(": ");
        // flushes out output buffers
        fflush(stdout);
        // reads command input from user
        fgets(input, INPUT_LENGTH, stdin);

    } while (input[0] == '\n' || input[0] == '#');

    // Tokenize the input
    char *token = strtok(input, " \n");

    // iterates over token
    while(token){
        // if '<' next input is the redirect to input file
        if(!strcmp(token,"<")){
            curr_command->input_file = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,">")){
            // if '>' next input is the redirect to output file
            curr_command->output_file = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,"&")){
            // if '&', background is True
            curr_command->is_bg = true;
        } else{
            // else add token as argument
            curr_command->argv[curr_command->argc++] = strdup(token);
    }
    // next token
    token=strtok(NULL," \n");
    }
    // returns pointer to command line structure that holds parsed data
    return curr_command;
}

// Function is a handler for SIGINT for child process
// Arguments: an int that is the signal number
// Returns: exits child process
void handleSIG_INT(int signo) {
    exit(1);
}

// Function is a handler for SIGINT for parent process
// Arguments: an int that is the signal number
// Returns: A string that prints the termination signal of process
void handleParentSIG_INT(int signo) {
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "terminated by signal %d\n", signo);
    write(STDOUT_FILENO, buffer, strlen(buffer));
} 

// Function is a handler for SIGTSTP
// Arguments: an int that is the signal number
// Returns: A string that toggles foreground-only mode
void handleSIG_STP(int signo) {
    char buffer[100];
    write(STDOUT_FILENO, "\n", 1);

    // if fgOnly mode is false
    if (fgOnly == 0) {
        // prints string when fgOnly mode was false (0), toggles mode to true (1)
        snprintf(buffer, sizeof(buffer), "Entering foreground-only mode (& is not ignored)\n");
        write(STDOUT_FILENO, buffer, strlen(buffer));
        fgOnly = 1;
    } else {
        // prints string when fgOnly mode was true (1), toggles mode to false (0)
        snprintf(buffer, sizeof(buffer), "Exiting foreground-only mode\n");
        write(STDOUT_FILENO, buffer, strlen(buffer));
        fgOnly = 0;
    }
    write(STDOUT_FILENO, ": ", 2);
    fflush(stdout);
}

// Function is a handler for SIGCHLD
// Arguments: an int that is the signal number
// Returns: For closed bg processes, prints string with termination status to terminal and removes bgProcesses array.
void handleSIG_CHLD(int signo) {
    int childStatus;
    pid_t childPid;

    // checks if open bg processes have terminated
    for (int i =0; i < bgCount; i++) {
        childPid = waitpid(bgProcesses[i], &childStatus, WNOHANG);
        // if child process terminated
        if (childPid > 0) {
            // remove process from array
            for (int j = i; j < bgCount; j++) {
                bgProcesses[j] = bgProcesses[j + 1];
            }
            // reduce count by 1
            bgCount = bgCount - 1;
         
            // print string for bg process that were terminated
            char buffer[100];   // initialize buffer for string
            if (WIFEXITED(childStatus)) {
                // prints str to buffer
                snprintf(buffer, sizeof(buffer), "background pid %d is done: exit value %d\n", 
                     childPid, WEXITSTATUS(childStatus));
            } else if (WIFSIGNALED(childStatus)) {
                // prints str to buffer
                snprintf(buffer, sizeof(buffer), "background pid %d is done: terminated by signal %d\n", 
                     childPid, WTERMSIG(childStatus));
            }
            // prints output to terminal
            write(STDOUT_FILENO, buffer, strlen(buffer));
        }
    }
}

int main() {
    struct command_line *curr_command;

    // signal handlers for parent process

    // signal handler for SIGINT
    struct sigaction SIGINT_action = {0};           // initialize SIGINT_action struct to be empty
    SIGINT_action.sa_handler = handleParentSIG_INT; // register signal handler
    sigfillset(&SIGINT_action.sa_mask);             // block all catchable signals while handler is running
    SIGINT_action.sa_flags = SA_RESTART;            // no flags set
    sigaction(SIGINT, &SIGINT_action, NULL);        // install signal handler

    // signal handler for SIGSTP
    struct sigaction SIGSTP_action = {0};           // initialize SIGSTP_action struct to be empty
    SIGSTP_action.sa_handler = handleSIG_STP;       // register signal handler
    sigfillset(&SIGSTP_action.sa_mask);             // block all catchable signals while handler is running
    SIGSTP_action.sa_flags = SA_RESTART;            // automatic restart of interrupted system call or library function
    sigaction(SIGTSTP, &SIGSTP_action, NULL);       // install signal handler

    struct sigaction SIGCHLD_action = {0};          // initialize SIGCHLD_action struct to be empty
    SIGCHLD_action.sa_handler = handleSIG_CHLD;     // initialize signal handler
    sigemptyset(&SIGCHLD_action.sa_mask);           // no signals blocked while handler is running
    SIGCHLD_action.sa_flags = SA_RESTART;           // restarts interrupted sys call
    sigaction(SIGCHLD, &SIGCHLD_action, NULL);      // install signal handler

    struct sigaction SIGTERM_action = {0};          // initialize SIGTERM_action struct to be empty
    SIGTERM_action.sa_handler = SIG_IGN;            // register singal handler to ignore SIGTERM
    sigaction(SIGTERM, &SIGTERM_action, NULL);      // initialize signal handler

    while(true) {
        curr_command = parse_input();               // parses users input

        // if user input is not empty
        if (curr_command->argc > 0) {
            // if input == "exit"
            if (strcmp(curr_command->argv[0], "exit") == 0) {
                if (curr_command->is_bg == true) {
                    // ensures built-in command runs in fg
                    curr_command->is_bg = false; 
                }
                // free command struct
                free(curr_command);
                // kills program
                kill(0, SIGKILL);
            } else if (strcmp(curr_command->argv[0], "status") == 0) {
                // checks if user input == "status"
                if (curr_command->is_bg == true) {
                    // ensures built-in command runs in fg
                    curr_command->is_bg = false;
                }
                // returns status of last fg process ran to terminal
                exitStatus(status);

            } else if (curr_command->argc < 3 && strcmp(curr_command->argv[0], "cd") == 0) {
                // if user input == "cd" and number of arguments < 3
                if (curr_command->is_bg == true) {
                    // ensures built-in command runs in fg
                    curr_command->is_bg = false; 
                }
                // if number of commands == 1, change directory to 'HOME'
                if (curr_command->argc == 1) {
                    chdir(getenv("HOME"));
                } else {
                    // change directory to argument passed after cd, searches PATH
                    if (chdir(curr_command->argv[1]) == -1) {
                        perror("chdir");
                    }
                }

            } else {
                // other commands

                int size = curr_command->argc;

                // allocates space for newargv, an array to hold user arguments
                char **newargv = (char **)malloc((size + 1) * sizeof(char *));

                // fills array with user command (args)
                for (int i = 0; i < size; i++ ) {
                    newargv[i] = curr_command->argv[i];
                    }
                // null terminates array
                newargv[size] = NULL;

                // child process
                int childStatus;
                pid_t childPid = fork();

                switch(childPid){
                    case -1:
                        // Failed forking
                        perror("fork()\n");
                        exit(1);
                    case 0:
                        // successful child process created
                        
                        // if bg process and fg toggle is false
                        if (curr_command->is_bg == true && fgOnly == 0) {
                            // bg child signal handlers

                            // signal handler for SIG_STP
                            struct sigaction SIGSTP_action = {0};      // initialize SIGSTP_action struct to be empty
                            SIGSTP_action.sa_handler = SIG_IGN;        // register singal handler to ignore SIGTERM
                            sigaction(SIGTSTP, &SIGSTP_action, NULL);  // initialize signal handler

                            // signal handler for SIG_INT
                            struct sigaction SIGINT_action = {0};      // initialize SIGINT_action structure to be empty
                            SIGINT_action.sa_handler = SIG_IGN;        // register signal handler to ignore SIGINT
                            sigaction(SIGINT, &SIGINT_action, NULL);   // initialize signal handler

                            // signal handler for SIG_TERM
                            struct sigaction SIGTERM_action = {0};     // intialize SIGTERM_action struct to be empty
                            SIGTERM_action.sa_handler = SIG_DFL;       // register signal handler to perform default action
                            sigaction(SIGTERM, &SIGTERM_action, NULL); // initialize signal handler
                            
                            // determine redirection for bg processes
                            backgroundChild(curr_command);
                        } else {
                            if (fgOnly == 1) {
                                // if foreground only mode, bg is false
                                curr_command->is_bg = false;
                            }

                            // signal handlers for fg child processes

                            // signal hander for SIG_STP
                            struct sigaction SIGSTP_action = {0};       // initialize SIGSTP_action struct to be empty
                            SIGSTP_action.sa_handler = SIG_IGN;         // register signal handler to ignore SIGSTP
                            sigaction(SIGTSTP, &SIGSTP_action, NULL);   // initialize signal handler

                            // signal handler SIG_INT
                            struct sigaction SIGINT_action = {0};       // initialize SIGINT_actions struct to be empty
                            SIGINT_action.sa_handler = handleSIG_INT;   // register signal handler
                            sigfillset(&SIGINT_action.sa_mask);         // block all catchable signals while handler is running
                            SIGINT_action.sa_flags = 0;                 // no flags set
                            sigaction(SIGINT, &SIGINT_action, NULL);    // initialize signal handler

                            // determine redirection for fg processes
                            foregroundChild(curr_command);
                        }

                        // Exec()
                        execvp(newargv[0], newargv);
                        // if error
                        perror("execvp");
                        free(newargv);
                        exit(1);
                        
                default:
                    // Parent process
                    if (curr_command->is_bg == true && fgOnly == 0) {
                        // adds background process to an array, to track termination
                        bgProcesses[bgCount] = childPid;
                        bgCount ++;
                        // prints background id to user
                        printf("background pid is %d\n", childPid);
                        fflush(stdout);
                    } else {
                        // if foreground process
                        waitpid(childPid, &childStatus, 0);
                        if(WIFEXITED(childStatus)){
                            // child exited normally, updates status
                            status = WEXITSTATUS(childStatus);
                        } else{
                            // child exited abnormally, updates status
                            status = WTERMSIG(childStatus);
                        }
                    }
                    break;
                }
            }
        }
        // frees curr_command structure for next user input
        free(curr_command);
    }

    return EXIT_SUCCESS;
}

// Functions is used to return the exit status or signal termination of the last fg process ran by the shell.
// Arguments: an int status, that tracks the status of fg processes.
// Returns: a str that is either a normally terminated process or an abnormally terminated process
void exitStatus(int status) {
    // returns if child was terminated normally, prints exit value
    if (WIFEXITED(status)) {
        printf("exit value %d\n", WEXITSTATUS(status));
        fflush(stdout);
    } else if (WIFSIGNALED(status)) {
        // returns if child was terminated abnormally, prints signal
        printf("terminated by signal %d\n", WTERMSIG(status));
        fflush(stdout);
    }
}

// Function is used if the child process is a background process
// Arguments: curr_command: is a pointer to the command_line structure, this is the command entered by the user.
// Returns: sets the stdin and stdout for bg process, sets to default path if input/output not specified by user.
void backgroundChild(struct command_line *curr_command) {
    // Redirections
    // if input file is specified
    if (curr_command->input_file != NULL) {
        // opens file for reading only
        int sourceFD = open(curr_command->input_file, O_RDONLY);
        // if error
        if (sourceFD == -1) {
            perror("open()");
            exit(1);
        }
        //redirects stdin to input file
        int result1 = dup2(sourceFD, 0);
        // if error
        if (result1 == -1) {
            perror("dup2");
            exit(1);
        }
    } else {
        // if input file not specified, sets to default "/dev/null"
        // opens for reading only
        int nullSourceFD = open("/dev/null", O_RDONLY);
        // if error
        if (nullSourceFD == -1) {
            perror("open()");
            exit(1);
        }
        // redirects stdin to input
        int result2 = dup2(nullSourceFD, 0);
        // if error
        if (result2 == -1) {
            perror("dup2");
            exit(1);
        }
    }

    // if output file is specified
    if (curr_command->output_file != NULL) {
        // opens/creates file for writing only
        int targetFD = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        // if error
        if (targetFD == -1) {
            perror("open()");
            exit(1);
        }
        // redirects stdout to output
        int result3 = dup2(targetFD, 1);
        // if error
        if (result3 == -1) {
            perror("dup2");
            exit(1);
        }
        
    } else {
        // if file not specified, sets to default, opens/creates for writing only
        int nullTargetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0640);
        // if error, sets exit status to 1
        if (nullTargetFD == -1) {
            perror("open()");
            exit(1);
        }
        // redirects output to default
        int result4 = dup2(nullTargetFD, 1);
        // if error
        if (result4 == -1) {
            perror("dup2");
            exit(1);
        }
    }
}

// Function is used if the child process is a foreground process
// Arguments: curr_command: is a pointer to the command_line structure, this is the command entered by the user.
// Returns: opens the sourceFD and targetFD if input and/or output file was entered by the user.
void foregroundChild(struct command_line *curr_command) {
    // Redirections
    // if input file was entered by user
    if (curr_command->input_file != NULL) {
        // opens file for reading only
        int sourceFD = open(curr_command->input_file, O_RDONLY);
        // if cannot open (error)
        if (sourceFD == -1) {
            perror("open()");
            status = 1;
            exit(1);
        }
        // redirects stdin to input file
        int result1 = dup2(sourceFD, 0);
        // if error
        if (result1 == -1) {
            perror("dup2");
            status = 1;
            exit(1);
        }
       
    } 

    // if output file was entered by user
    if (curr_command->output_file != NULL) {
        // opens/creates file for writing only
        int targetFD = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        // if error
        if (targetFD == -1) {
            perror("open()");
            status = 1;
            exit(1);
        }
        // redirect stdout to output file
        int result3 = dup2(targetFD, 1);
        // if error
        if (result3 == -1) {
            perror("dup2");
            status = 1;
            exit(1);
        }
        
    } 
}