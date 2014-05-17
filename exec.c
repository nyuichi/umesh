#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "parse.h"

extern char **environ;
char FC_BUF[256];

const char *
find_command(const char *name)
{
  const char *path;
  size_t len;
  struct stat st;

  if (strchr(name, '/') != NULL) {
    return name;                /* may be absolute path */
  }
  path = getenv("PATH");
  if (path == NULL) {
    return NULL;
  }
  while ((len = strcspn(path, ":")) != 0) {
    strncpy(FC_BUF, path, len);
    FC_BUF[len] = '\0';
    strcat(FC_BUF, "/");
    strcat(FC_BUF, name);

    if (stat(FC_BUF, &st) == 0) {
      return FC_BUF;
    }

    if (path[len] == '\0') {
      break;
    }

    path += len + 1;
  }

  return NULL;
}

int
open_pipes(process *pr)
{
  process *prev;
  int fd[2], flags;

  /* prepare pipes and redirects */
  for (prev = NULL; pr != NULL; prev = pr, pr = pr->next) {

    pr->fd0 = 0;
    pr->fd1 = 1;

    if (prev != NULL) {
      pr->fd0 = fd[0];
    }
    if (pr->next != NULL) {
      if (pipe(fd) != 0) {
        return -1;
      }
      pr->fd1 = fd[1];
    }

    if (pr->input_redirection != NULL) {
      pr->fd0 = open(pr->input_redirection, O_RDONLY);
    }
    if (pr->output_redirection != NULL) {
      flags = O_WRONLY | O_CREAT;
      switch (pr->output_option) {
      case APPEND:
        flags |= O_APPEND;
        break;
      case TRUNC:
        flags |= O_TRUNC;
        break;
      }
      pr->fd1 = open(pr->output_redirection, flags, 0777);
    }
  }

  return 0;
}

int
exec_process_list(process *pr_list)
{
  process *pr;
  pid_t pid, pgid;
  const char *cmd;

  pgid = -1;

  for (pr = pr_list; pr != NULL; pr = pr->next) {
    if ((pid = fork()) == -1) {
      return -1;
    }
    else if (pid == 0) {
      /* child process */

      dup2(pr->fd0, 0);
      dup2(pr->fd1, 1);

      cmd = find_command(pr->program_name);
      if (cmd == NULL) {
        return -1;
      }

      execve(cmd, pr->argument_list, environ);

      close(pr->fd0);
      close(pr->fd1);

      exit(0);
    }
    else {
      /* parent process */

      if (pr->fd0 != 0) close(pr->fd0);
      if (pr->fd1 != 1) close(pr->fd1);

      pr->pid = pid;

      if (pgid == -1) {
        pgid = pid;
      }
      setpgid(pid, pgid);
    }
  }

  return pgid;
}

int
exec_job(process *pr_list, int fg)
{
  process *pr;
  pid_t pgid;
  int status;

  /* pipes & redirects */
  if (open_pipes(pr_list) < 0) {
    return -1;
  }

  /* fork & exec */
  if ((pgid = exec_process_list(pr_list)) < 0) {
    return -1;
  }

  if (fg) {

    if (tcsetpgrp(0, pgid) == -1) {
      return -1;
    }

    /* wait! */
    for (pr = pr_list; pr != NULL; pr = pr->next) {
      while (waitpid(pr->pid, &status, WUNTRACED) == -1) {
        if (errno == EINTR)
          ;                    /* retry when interrupted by SIGCHLD */
        else if (errno == ECHILD)
          break;                /* already discarded by do_sigchld */
        else
          perror("waitpid");
      }
    }
    if (WIFSTOPPED(status)) {
      puts("A job has been stopped");
    }

    if (tcsetpgrp(0, getpgid(0)) == -1) {
      return -1;
    }

  }

  return 0;
}

void
exec_job_list(job *job_list)
{
  job *jb;

  for (jb = job_list; jb != NULL; jb = jb->next) {
    if (exec_job(jb->process_list, jb->mode == FOREGROUND) < 0) {
      abort();
    }
  }
}
