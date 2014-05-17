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
#include "xvect.h"

xvect cz_jobs;                 /* list of pgids of suspended jobs */

static void
post_job_suspend(pid_t pid, int in_sa_handler)
{
  sigset_t sigset;
  pid_t pgid;
  size_t i;

  if (! in_sa_handler) {
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
  }

  pgid = getpgid(pid);
  for (i = 0; i < xv_size(&cz_jobs);) {
    if (*(pid_t *)xv_get(&cz_jobs, i) == pgid) {
      xv_splice(&cz_jobs, i, 1);
    } else {
      ++i;
    }
  }
  xv_push(&cz_jobs, &pgid);

  if (! in_sa_handler) {
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  }
}

static void
do_sigchld(int sig)
{
  pid_t pid;
  int status;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFSTOPPED(status)) {
      post_job_suspend(pid, 1);
    }
  }
}

static void
do_sigtstp(int sig)
{
  /* pass */
}

static void
exec_signal_init(void)
{
  struct sigaction ign, chld, stp;

  ign.sa_handler = SIG_IGN;
  ign.sa_flags = 0;
  sigemptyset(&ign.sa_mask);
  sigaction(SIGTTOU, &ign, NULL);

  /**
   * To avoid SIG_IGN being inherited to child processes,
   * registering a newly defined empty handler to SIGTSTP and SIGINT
   */
  stp.sa_handler = do_sigtstp;
  stp.sa_flags = 0;
  sigemptyset(&stp.sa_mask);
  sigaction(SIGTSTP, &stp, NULL);
  sigaction(SIGINT, &stp, NULL);

  chld.sa_handler = do_sigchld;
  chld.sa_flags = 0;
  sigemptyset(&chld.sa_mask);
  sigaction(SIGCHLD, &chld, NULL);
}

void
exec_init(void)
{
  xv_init(&cz_jobs, sizeof(pid_t));

  exec_signal_init();
}

void
exec_fini(void)
{
  xv_destroy(&cz_jobs);
}

void
exec_bg(void)
{
  sigset_t sigset;
  pid_t pgid;

  sigemptyset(&sigset);
  sigaddset(&sigset, SIGCHLD);
  sigprocmask(SIG_BLOCK, &sigset, NULL);

  if (xv_size(&cz_jobs) > 0) {
    pgid = *(pid_t *)xv_pop(&cz_jobs);
    kill(-pgid, SIGCONT);
  } else {
    fprintf(stderr, "no suspended process\n");
  }

  sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

extern char **environ;

const char *
find_command(const char *name)
{
  static char BUF[256];
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
    strncpy(BUF, path, len);
    BUF[len] = '\0';
    strcat(BUF, "/");
    strcat(BUF, name);

    if (stat(BUF, &st) == 0) {
      return BUF;
    }

    if (path[len] == '\0') {
      break;
    }

    path += len + 1;
  }

  return NULL;
}

static int
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

static int
exec_process_list(process *pr_list)
{
  process *pr;
  pid_t pid, pgid;
  const char *cmd;

  if (open_pipes(pr_list) < 0) {
    return -1;
  }

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
        fprintf(stderr, "program not found: %s\n", pr->program_name);
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

static int
exec_job(process *pr_list, int mode)
{
  process *pr;
  pid_t pgid;
  int status;

  if ((pgid = exec_process_list(pr_list)) < 0) {
    return -1;
  }

  if (mode == FOREGROUND) {

    if (tcsetpgrp(0, pgid) == -1) {
      return -1;
    }

    /* wait! */
    status = 0;
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
      post_job_suspend(pr_list->pid, 0);
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
    if (exec_job(jb->process_list, jb->mode) < 0) {
      abort();
    }
  }
}
