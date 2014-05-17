#include <stdio.h>
#include <stdlib.h>
#include "parse.h"

static int print_process(process* pr) {

    int index = 0;

    if(pr->program_name == NULL) {
        return -1;
    }

    printf("* program = %s\n", pr->program_name);

    if(pr->argument_list != NULL) {
        while(pr->argument_list[index] != NULL) {
                printf( "  - arg[ %d ] = %s\n", index, pr->argument_list[index]);
                index++;
        }
    }

    if(pr->input_redirection != NULL) {
        printf("  - input redirection = %s\n", pr->input_redirection);
    }

    if (pr->output_redirection != NULL)
        printf("  - output redirection = %s [ %s ]\n",
               pr->output_redirection,
               pr->output_option == TRUNC ? "trunc" : "append" );

    return 0;
}

void print_job_list(job* job_list) {
    int      index;
    job*     jb;
    process* pr;

    for(index = 0, jb = job_list; jb != NULL; jb = jb->next, ++index) {
        printf("id %d [ %s ]\n", index,
               jb->mode == FOREGROUND ? "foreground" : "background" );

        for(pr = jb->process_list; pr != NULL; pr = pr->next) {
            if(print_process( pr ) < 0) {
                    exit(EXIT_FAILURE);
            }
            if(jb->next != NULL) {
                printf( "\n" );
            }
        }
    }
}
