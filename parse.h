#ifndef __PARSE_H__
#define __PARSE_H__

#include <unistd.h>

#define PROMPT "umesh$ "
#define NAMELEN 32
#define ARGLSTLEN 16
#define LINELEN 256

typedef enum write_option_ {
  TRUNC,
  APPEND,
} write_option;

typedef struct process_ {
  char*        program_name;
  char**       argument_list;

  char*        input_redirection;

  write_option output_option;
  char*        output_redirection;

  struct process_* next;

  /* runtime attribute */
  pid_t pid;
  int fd0;
  int fd1;

} process;

typedef enum job_mode_ {
  FOREGROUND,
  BACKGROUND,
} job_mode;

typedef struct job_ {
  job_mode     mode;
  process*     process_list;
  struct job_* next;
} job;

typedef enum parse_state_ {
  ARGUMENT,
  IN_REDIRCT,
  OUT_REDIRCT_TRUNC,
  OUT_REDIRCT_APPEND,
} parse_state;

char* get_line(char *, int);
job* parse_line(char *);
void free_job(job *);

#endif
