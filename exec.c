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

typedef struct bg_job {
  pid_t pgid;
  int status;
} bg_job;

enum {
  RUNNING,
  SUSPENDED
};

xvect bg_jobs;                 /* list of bg_job */

static void
post_job_suspend(pid_t pgid, int in_sa_handler)
{
  sigset_t sigset;
  bg_job job;
  size_t i;

  if (! in_sa_handler) {
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
  }

  for (i = 0; i < xv_size(&bg_jobs); ++i) {
    job = *(bg_job *)xv_get(&bg_jobs, i);
    if (job.pgid == pgid) {
      job.status = SUSPENDED;
      break;
    }
  }
  if (i == xv_size(&bg_jobs)) {
    job.pgid = pgid;
    job.status = SUSPENDED;
    xv_push(&bg_jobs, &job);
  }

  if (! in_sa_handler) {
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  }
}

static void
do_sigchld(int sig)
{
  pid_t pid, pgid;
  int status;
  xvect dying_jobs;
  size_t i, j;

  xv_init(&dying_jobs, sizeof(pid_t));

  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
    if (WIFSTOPPED(status)) {
      post_job_suspend(getpgid(pid), 1);
    }
    else {
      pgid = getpgid(pid);
      xv_push(&dying_jobs, &pgid);
    }
  }

  for (i = 0; i < xv_size(&dying_jobs); ++i) {
    pgid = *(pid_t *)xv_get(&dying_jobs, i);
    if (waitpid(-pgid, &status, WNOHANG | WUNTRACED) == -1 && errno == ECHILD) { /* if the job of pgid is completely dead */
      for (j = 0; j < xv_size(&bg_jobs); ++j) {
        if (((bg_job *)xv_get(&bg_jobs, j))->pgid == pgid) {
          break;
        }
      }
      if (j != xv_size(&bg_jobs)) {
        xv_splice(&bg_jobs, j, 1);
      }
    }
  }

  xv_destroy(&dying_jobs);
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
  xv_init(&bg_jobs, sizeof(bg_job));

  exec_signal_init();
}

void
exec_fini(void)
{
  xv_destroy(&bg_jobs);
}

void
exec_bg(void)
{
  sigset_t sigset;
  bg_job *job;
  size_t i;

  sigemptyset(&sigset);
  sigaddset(&sigset, SIGCHLD);
  sigprocmask(SIG_BLOCK, &sigset, NULL);

  for (i = xv_size(&bg_jobs) - 1; i >= 0; --i) {
    job = (bg_job *)xv_get(&bg_jobs, i);
    if (job->status == SUSPENDED) {
      break;
    }
  }
  if (i != -1) {
    kill(-job->pgid, SIGCONT);
    job->status = RUNNING;
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

static void
wait_for_job(pid_t pgid)
{
  pid_t pid;
  int status;

  do {
    pid = waitpid(-pgid, &status, WUNTRACED);
    if (pid == -1) {
      if (errno == EINTR) {
        continue;               /* retry when interrupted by SIGCHLD */
      } else if (errno == ECHILD) {
        continue;                  /* already discarded by do_sigchld */
      } else {
        perror("waitpid");
      }
    }
    if (pid > 0) {
      if (WIFSTOPPED(status)) {
        post_job_suspend(pgid, 0);
        break;
      }
    }
  } while (pid > 0);
}

static int
exec_job(process *pr_list, int mode)
{
  pid_t pgid;

  if ((pgid = exec_process_list(pr_list)) < 0) {
    return -1;
  }

  if (mode == FOREGROUND) {
    /* fg */

    if (tcsetpgrp(0, pgid) == -1) {
      return -1;
    }

    wait_for_job(pgid);

    if (tcsetpgrp(0, getpgid(0)) == -1) {
      return -1;
    }
  }
  else {
    /* bg */

    bg_job job;

    job.pgid = pgid;
    job.status = RUNNING;
    xv_push(&bg_jobs, &job);
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
