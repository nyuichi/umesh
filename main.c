#include <string.h>
#include <stdio.h>

#include <signal.h>
#include <sys/wait.h>

#include "parse.h"
#include "xvect.h"

void print_job_list(job *);
void exec_init(void);
void exec_fini(void);
void exec_bg(void);
void exec_job_list(job *);

int
main(int argc, char *argv[]) {
  char s[LINELEN];
  job *curr_job;

  exec_init();

  while (get_line(s, LINELEN)) {
    if (! strcmp(s, "exit\n"))
      break;

    if (! strcmp(s, "bg\n")) {
      exec_bg();
    }
    else {
      curr_job = parse_line(s);

#if 0
      print_job_list(curr_job);
#endif

      exec_job_list(curr_job);
    }

    free_job(curr_job);
  }

  exec_fini();

  return 0;
}
