#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/init.h"

#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

// #include <sys/types.h>
// #include <unistd.h>

#define MAX_ARGS 3
#define USER_VADDR_BOTTOM ((void *) 0x08048000)

static void syscall_handler (struct intr_frame *);

int get_physical_addr(const void *vaddr);

void check_valid_ptr (const void *vaddr);



void
syscall_init (void)
{
  lock_init(&files_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  int arg[MAX_ARGS];

  // Verifies stack pointer and reads system call from it
  check_ptr(f->esp);
  int esp = get_physical_addr((const void*) f->esp);

  // Cast pointer as an int pointer and read system call
  switch (* (int *) esp)
  {
    case SYS_HALT:
    {
      halt ();
      break;
    }
    case SYS_EXIT:
    {
      pull_args(f, &arg[0], 1);
      exit(arg[0]);
      break;
    }
    case SYS_EXEC:
    {
      pull_args(f, &arg[0], 1);
      check_string((const void *) arg[0]);
      arg[0] = get_physical_addr((const void *) arg[0]);
      f->eax = exec((const char *) arg[0]);
      break;
    }
    case SYS_WAIT:
    {
      pull_args(f, &arg[0], 1);
	    f->eax = wait(arg[0]);
	    break;
    }
    case SYS_CREATE:
    {
      pull_args(f, &arg[0], 2);
      check_string((const void *) arg[0]);
      arg[0] = get_physical_addr((const void *) arg[0]);
      f->eax = create((const char *)arg[0], (unsigned) arg[1]);
      break;
    }
    case SYS_REMOVE:
    {
      pull_args(f, &arg[0], 1);
	    arg[0] = get_physical_addr((const void *) arg[0]);
	    f->eax = remove((const char *) arg[0]);
	    break;
    }
    case SYS_OPEN:
    {
      pull_args(f, &arg[0], 1);
      arg[0] = get_physical_addr((const void *) arg[0]);
      f->eax = open((const char *) arg[0]);
      break;
    }
    case SYS_FILESIZE:
    {
      pull_args(f, &arg[0], 1);
    	f->eax = filesize(arg[0]);
    	break;
    }
    case SYS_READ:
    {
      pull_args(f, &arg[0], 3);
    	check_buffer((void *) arg[1], (unsigned) arg[2]);
    	arg[1] = get_physical_addr((const void *) arg[1]);
    	f->eax = read(arg[0], (void *) arg[1], (unsigned) arg[2]);
    	break;
    }
    case SYS_WRITE:
    {
      pull_args(f, &arg[0], 3);
	    check_buffer((void *) arg[1], (unsigned) arg[2]);
	    arg[1] = get_physical_addr((const void *) arg[1]);
	    f->eax = write(arg[0], (const void *) arg[1], (unsigned) arg[2]);
      break;
    }
    case SYS_SEEK:
    {
      pull_args(f, &arg[0], 2);
	    seek(arg[0], (unsigned) arg[1]);
	    break;
    }
    case SYS_TELL:
    {
      pull_args(f, &arg[0], 1);
	     f->eax = tell(arg[0]);
	      break;
    }
    case SYS_CLOSE:
    {
      pull_args(f, &arg[0], 1);
    	close(arg[0]);
    	break;
    }
  }
}

/* System call functions*/
void
halt (void)
{
  power_off ();
}

void
exit (int status)
{
  struct thread *curr= thread_current();
  curr->exit_status=status;
  thread_exit();
  // NOT_REACHED();
}

pid_t
exec (const char *cmd_line)
{
  pid_t pid = process_execute(cmd_line);
  struct thread* child = get_child_process(pid);
  if (!child)
  {
    return -1;
  }
  if (child->load_status == 0)
  {
    // printf("Waiting for child to load\n");
    sema_down(&thread_current()->load_lock);
  }
  // If load failed
  if (child->load_status == -1)
  {
    remove_child_process(child);
    return -1;
  }
  return pid;
}

int
wait (pid_t pid)
{
  return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size)
{
  if (file == NULL)
    exit(-1);
  lock_acquire(&files_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&files_lock);
  return success;
}

int
filesize (int fd)
{
  lock_acquire(&files_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&files_lock);
      return -1;
    }
  int size = file_length(f);
  lock_release(&files_lock);
  return size;
}

int
read(int fd, void *buffer, unsigned size)
{
  if (fd == 0)
    {
      unsigned i;
      uint8_t* local_buffer = (uint8_t *) buffer;
      for (i = 0; i < size; i++)
	{
	  local_buffer[i] = input_getc();
	}
      return size;
    }
  lock_acquire(&files_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&files_lock);
      return -1;
    }
  int bytes = file_read(f, buffer, size);
  lock_release(&files_lock);
  return bytes;
}


int
write(int fd, void *buffer, unsigned size)
{

  if (fd == 1)
    {
      putbuf(buffer, size);
      return size;
    }
  lock_acquire(&files_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&files_lock);
      return -1;
    }
  int bytes = file_write(f, buffer, size);
  lock_release(&files_lock);
  return bytes;
}

bool
remove (const char *file)
{
  lock_acquire(&files_lock);
  bool success = filesys_remove(file);
  lock_release(&files_lock);
  return success;
}

int open (const char *file)
{
  if (file==NULL)
    exit(-1);
  lock_acquire(&files_lock);
  struct file *f = filesys_open(file);
  if (!f)
    {
      lock_release(&files_lock);
      return -1;
    }
  int fd = process_add_file(f);
  lock_release(&files_lock);
  return fd;
}


void
seek (int fd, unsigned position)
{
  lock_acquire(&files_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&files_lock);
      return;
    }
  file_seek(f, position);
  lock_release(&files_lock);
}

unsigned
tell (int fd)
{
  lock_acquire(&files_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&files_lock);
      return -1;
    }
  off_t offset = file_tell(f);
  lock_release(&files_lock);
  return offset;
}

void
close (int fd)
{
  lock_acquire(&files_lock);
  process_close_file(fd);
  lock_release(&files_lock);
}

void
pull_args (struct intr_frame *f, int *arg, int n)
{
  int *ptr;
  ptr = (int *) f->esp;
  int i;
  for (i = 0; i < n; i++)
  {
    ptr++;
    check_ptr((const void *) ptr);
    arg[i] = *ptr;
  }
}


void check_ptr (const void *vaddr)
{
  if (!is_user_vaddr(vaddr) || vaddr < USER_VADDR_BOTTOM)
    {
      exit(-1);
    }
}

void check_buffer (void* buffer, unsigned size)
{
  int i;
  char* buf_ptr = (char *) buffer;
  for (i = 0; i < size; i++)
    {
      check_ptr((const void*) buf_ptr);
      buf_ptr++;
    }
}

void check_string (const void* str)
{
  while (* (char *) get_physical_addr(str) != 0)
    {
      str = (char *) str + 1;
    }
}


int get_physical_addr(const void *vaddr)
{
  check_ptr(vaddr);
  void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
  if (!ptr)
    {
      exit(-1);
    }
  return (int) ptr;
}
