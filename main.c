#include <string.h>

#include <signal.h>
#include <sys/wait.h>

#include "parse.h"
#include "xvect.h"

void print_job_list(job *);
void exec_job_list(job *);

xvect cz_pgids;                 /* can have some duplicates */

void
do_sigchld(int sig)
{
  pid_t pid, pgid;
  int status;
  size_t i;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFSTOPPED(status)) {
      pgid = getpgid(pid);
      for (i = 0; i < xv_size(&cz_pgids);) {
        if (*(pid_t *)xv_get(&cz_pgids, i) == pgid) {
          xv_splice(&cz_pgids, i, 1);
        } else {
          ++i;
        }
      }
      xv_push(&cz_pgids, &pgid);
    }
  }
}

void
do_sigtstp(int sig)
{
  /* pass */
}

void
init(void)
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

int main(int argc, char *argv[], char *argp[]) {
    char s[LINELEN];
    job *curr_job;

    xv_init(&cz_pgids, sizeof(pid_t));

    init();

    while(get_line(s, LINELEN)) {
        if(!strcmp(s, "exit\n"))
            break;

        curr_job = parse_line(s);

#if 0
        print_job_list(curr_job);
#endif

        exec_job_list(curr_job);

        free_job(curr_job);
    }

    xv_destroy(&cz_pgids);

    return 0;
}
