#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"


bool install_page (void *upage, void *kpage, bool writable);

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct thread* get_child_process (int);
void remove_child_process (struct thread *);

/* Process file management functions */
int process_add_file (struct file *);
struct file* process_get_file (int);
void process_close_file (int);
#endif /* userprog/process.h */
