#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"

typedef int pid_t;

void syscall_init (void);

/* Shared by syscall.c and process.c */
struct lock files_lock; /* Lock that only allows single access to file system */

/* System call handler functions are only used in syscall.c
      with the exception of exit(-1) called by interrupt.
      We used exit to smooth out error handling for the
      exceptions, but a better approach might be to use
      a different function and keep the system call handlers
      strictly within syscall.                          */

/* System call handlers */
void halt (void);
void exit (int);
pid_t exec (const char *);
int wait (pid_t pid);
bool create (const char *, unsigned);
bool remove (const char *);
int open (const char *);
int filesize (int);
int read(int, void *, unsigned);
int write (int, const void *, unsigned);
void seek (int, unsigned);
unsigned tell (int);
void close (int);

#endif /* userprog/syscall.h */
