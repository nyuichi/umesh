#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include "parse.h"

char* get_line(char *s, int size) {

    printf(PROMPT);

    while(fgets(s, size, stdin) == NULL) {
        if(errno == EINTR)
            continue;
        return NULL;
    }

    return s;
}

static char* initialize_program_name(process *p) {

    if(!(p->program_name = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

    memset(p->program_name, 0, NAMELEN);

    return p->program_name;
}

static char** initialize_argument_list(process *p) {

    if(!(p->argument_list = (char**)malloc(sizeof(char*)*ARGLSTLEN)))
        return NULL;

    int i;
    for(i=0; i<ARGLSTLEN; i++)
        p->argument_list[i] = NULL;

    return p->argument_list;
}

static char* initialize_argument_list_element(process *p, int n) {

   if(!(p->argument_list[n] = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

   memset(p->argument_list[n], 0, NAMELEN);

   return p->argument_list[n];
}

static char* initialize_input_redirection(process *p) {

    if(!(p->input_redirection = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

    memset(p->input_redirection, 0, NAMELEN);

    return p->input_redirection;
}

static char* initialize_output_redirection(process *p) {

    if(!(p->output_redirection = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

    memset(p->output_redirection, 0, NAMELEN);

    return p->output_redirection;
}

static process* initialize_process() {

    process *p;

    if((p = (process*)malloc(sizeof(process))) == NULL)
        return NULL;

    initialize_program_name(p);
    initialize_argument_list(p);
    initialize_argument_list_element(p, 0);
    p->input_redirection = NULL;
    p->output_option = TRUNC;
    p->output_redirection = NULL;
    p->next = NULL;

    return p;
}

static job* initialize_job() {

    job *j;

    if((j = (job*)malloc(sizeof(job))) == NULL)
        return NULL;

    j->mode = FOREGROUND;
    j->process_list = initialize_process();
    j->next = NULL;

    return j;
}

static void free_process(process *p) {

    if(!p) return;

    free_process(p->next);

    if(p->program_name) free(p->program_name);
    if(p->input_redirection) free(p->input_redirection);
    if(p->output_redirection) free(p->output_redirection);

    if(p->argument_list) {
        int i;
        for(i=0; p->argument_list[i] != NULL; i++)
            free(p->argument_list[i]);
        free(p->argument_list);
    }

    free(p);
}

void free_job(job *j) {

    if(!j) return;

    free_job(j->next);

    free_process(j->process_list);

    free(j);
}

/* parser */
job* parse_line(char *buf) {

    job *curr_job = NULL;
    process *curr_prc = NULL;
    parse_state state = ARGUMENT;
    int index=0, arg_index=0;

    while(*buf != '\n') {
        if(*buf == ' ' || *buf == '\t') {
            buf++;
            if(index) {
                index = 0;
                state = ARGUMENT;
                ++arg_index;
            }
        }
        else if(*buf == '<') {
            state = IN_REDIRCT;
            buf++;
            index = 0;
        } else if(*buf == '>') {
            buf++;
            index = 0;
            if(state == OUT_REDIRCT_TRUNC) {
                state = OUT_REDIRCT_APPEND;
                if(curr_prc)
                    curr_prc->output_option = APPEND;
            }
            else {
                state = OUT_REDIRCT_TRUNC;
            }
        } else if(*buf == '|') {
            state = ARGUMENT;
            buf++;
            index = 0;
            arg_index = 0;
            if(curr_job) {
                strcpy(curr_prc->program_name,
                       curr_prc->argument_list[0]);
                curr_prc->next = initialize_process();
                curr_prc = curr_prc->next;
            }
        }
        else if(*buf == '&') {
            buf++;
            if(curr_job) {
                curr_job->mode = BACKGROUND;
                break;
            }
        }
        else if(state == ARGUMENT) {
            if(!curr_job) {
                curr_job = initialize_job();
                curr_prc = curr_job->process_list;
            }

            if(!curr_prc->argument_list[arg_index])
                initialize_argument_list_element(curr_prc, arg_index);

            curr_prc->argument_list[arg_index][index++] = *buf++;
        } else if(state == IN_REDIRCT) {
            if(!curr_prc->input_redirection)
                initialize_input_redirection(curr_prc);

            curr_prc->input_redirection[index++] = *buf++;
        } else if(state == OUT_REDIRCT_TRUNC || state == OUT_REDIRCT_APPEND) {
            if(!curr_prc->output_redirection)
                initialize_output_redirection(curr_prc);

            curr_prc->output_redirection[index++] = *buf++;
        }
    }

    if(curr_prc)
        strcpy(curr_prc->program_name, curr_prc->argument_list[0]);

    return curr_job;
}
