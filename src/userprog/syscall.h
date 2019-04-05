#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"

typedef int pid_t;

void syscall_init (void);

struct lock files_lock;


/* System calls */
void halt (void);
void exit (int);
int wait (pid_t pid);
bool create (const char *, unsigned);
pid_t exec (const char *);

bool remove (const char *);


void seek (int, unsigned);
unsigned tell (int);
void close (int);



/* Helper functions */
void pull_args(struct intr_frame *, int*, int);
void check_ptr(const void*);

#endif /* userprog/syscall.h */
