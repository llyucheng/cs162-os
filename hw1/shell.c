#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#include "tokenizer.h"

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_exec(struct tokens *tokens);
void cmd_exec_helper(char *str, struct tokens *tokens);
void cmd_output_direction(char *argv[], char *filename);
void cmd_input_direction(char *argv[], char *filename);
int detect_out_direction(struct tokens *tokens);
int detect_in_direction(struct tokens *tokens);
void signal_ignore();
void signal_default();
bool detect_background_proce(struct tokens *tokens);
void cmd_exec_background(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", ""},
  {cmd_cd, "cd", ""}
};

/* Prints a helpful description for the given command */
int cmd_help(struct tokens *tokens) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(struct tokens *tokens) {
  exit(0);
}

int cmd_pwd(struct tokens *tokens) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "%s\n", cwd);
  return 1;
}

int cmd_cd(struct tokens *tokens) {
  chdir(tokens_get_token(tokens, 1));
  return 1;
}

int cmd_exec(struct tokens *tokens) {
  int out = detect_out_direction(tokens);
  int in = detect_in_direction(tokens);
  if (out != -1) {
    char *argv[out + 1];
    int j;
    for (j = 0; j < out; j++) {
      argv[j] = tokens_get_token(tokens, j);
    }
    argv[out] = NULL;
    char *filename = tokens_get_token(tokens, out + 1);
    cmd_output_direction(argv, filename);
  } else if (in != -1) {
    char *argv[in + 1];
    int j;
    for (j = 0; j < in; j++) {
      argv[j] = tokens_get_token(tokens, j);
    }
    argv[in] = NULL;
    char *filename = tokens_get_token(tokens, in + 1);
    cmd_input_direction(argv, filename);
  } else {
    cmd_exec_helper(tokens_get_token(tokens, 0), tokens);
    char *path = getenv("PATH");
    char *pch;
    pch = strtok(path, ":");
    while (pch != NULL) {
      char *str = malloc(100);
      strcat(str, pch);
      strcat(str, "/");
      strcat(str, tokens_get_token(tokens, 0));
      pch = strtok(NULL, ":");
      cmd_exec_helper(str, tokens);  
    }
  }
  return 1;
}

void cmd_exec_helper(char *str, struct tokens *tokens) {
  char *argv[tokens_get_length(tokens) + 1];
  int i = 0;
  for (; i < tokens_get_length(tokens); i++) {
    argv[i] = tokens_get_token(tokens, i);
  }
  argv[tokens_get_length(tokens)] = NULL;
  execv(str, argv);
}

void cmd_exec_background(struct tokens *tokens) {
  //TODO need refactor exec code
}

int detect_out_direction(struct tokens *tokens) {
  int i = 0;
  for (; i < tokens_get_length(tokens); i++) {
    if (strcmp(tokens_get_token(tokens, i), ">") == 0) {
      return i;
    }
  }
  return -1;
}

void cmd_output_direction(char *argv[], char *filename) {
  int pipefd[2];
  pipe(pipefd);
  if (fork() == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], 1);
    dup2(pipefd[1], 2);
    close(pipefd[1]); 
    execv(argv[0], argv);
    char *path = getenv("PATH");
    char *pch;
    pch = strtok(path, ":");
    while (pch != NULL) {
      char *str = malloc(100);
      strcat(str, pch);
      strcat(str, "/");
      strcat(str, argv[0]);
      pch = strtok(NULL, ":");
      execv(str, argv);  
    }
  } else {
    char buffer[1024];
    close(pipefd[1]);
    while (read(pipefd[0], buffer, sizeof(buffer)) != 0) {
      FILE *f = fopen(filename, "w");
      fputs(buffer, f);
      fclose(f);
    }
  }
}

int detect_in_direction(struct tokens *tokens) {
  int i = 0;
  for (; i < tokens_get_length(tokens); i++) {
    if (strcmp(tokens_get_token(tokens, i), "<") == 0) {
      return i;
    }
  }
  return -1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

void cmd_input_direction(char *argv[], char *filename) {
  if (fork() == 0) {
    FILE *f = fopen(filename, "r");
    dup2(fileno(f),fileno(stdin));
    fclose(f);
    execvp(argv[0], argv);
  }

}

void signal_ignore() {
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGKILL, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
}

void signal_default() {
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGKILL, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}

bool detect_background_proce(struct tokens *tokens) {
  if (strcmp(tokens_get_token(tokens, tokens_get_length(tokens) - 1), "&") == 0) {
    return true;
  } else {
    return false;
  }
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(int argc, char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);
  
  signal_ignore();

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);
    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      pid_t fpid;
      int status;
      bool background = false;
      if (fork() == 0) {
        signal_default();
        if (detect_background_proce(tokens)) {
          background = true;

        } else {
          cmd_exec(tokens);
        }
      } else {
        signal_ignore();
        if (!background) {
          fpid = wait(&status);
        }
        background = false;
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
