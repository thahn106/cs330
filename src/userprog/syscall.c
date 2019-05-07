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
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);

/* Helper functions */
void pull_args(struct intr_frame *, int*, int);
int get_physical_addr(const void *, void*);
bool check_ptr(const void *, void*);
void check_buffer (void *, unsigned, void*);
void check_string (const void *, void*);

void
syscall_init (void)
{
  lock_init(&files_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  /* Stores args for system calls */
  int arg[3];

  /* Verifies stack pointer and reads system call from it */
  void *esp = f->esp;
  // int esp = get_physical_addr((const void*) f->esp);

  /* Cast pointer as an int pointer and deference to get system call */
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
      check_string((const void *) arg[0], esp);
      // arg[0] = get_physical_addr((const void *) arg[0]);
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
      check_string((const void *) arg[0], esp);
      // arg[0] = get_physical_addr((const void *) arg[0]);
      f->eax = create((const char *)arg[0], (unsigned) arg[1]);
      break;
    }
    case SYS_REMOVE:
    {
      pull_args(f, &arg[0], 1);
	    // arg[0] = get_physical_addr((const void *) arg[0]);
	    f->eax = remove((const char *) arg[0]);
	    break;
    }
    case SYS_OPEN:
    {
      pull_args(f, &arg[0], 1);
      // arg[0] = get_physical_addr((const void *) arg[0]);
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
    	check_buffer((void *) arg[1], (unsigned) arg[2], esp);
    	// arg[1] = get_physical_addr((const void *) arg[1]);
    	f->eax = read(arg[0], (void *) arg[1], (unsigned) arg[2]);
    	break;
    }
    case SYS_WRITE:
    {
      pull_args(f, &arg[0], 3);
	    check_buffer((void *) arg[1], (unsigned) arg[2], esp);
	    // arg[1] = get_physical_addr((const void *) arg[1]);
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
    case SYS_MMAP:
    {
      pull_args(f, &arg[0], 2);
      f->eax = mmap(arg[0], (const void *) arg[1]);
      break;
    }
    case SYS_MUNMAP:
    {
      pull_args(f, &arg[0], 1);
      munmap(arg[0]);
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
}

pid_t
exec (const char *cmd_line)
{
  pid_t pid = process_execute(cmd_line);
  struct thread* child = get_child_process(pid);

  /* If child failed to spawn throw error */
  if (!child)
    return -1;

  /* Wait for child to load */
  if (child->load_status == 0)
    sema_down(&thread_current()->load_lock);

  /* Free child resources if child failed to load */
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

bool
remove (const char *file)
{
  lock_acquire(&files_lock);
  bool success = filesys_remove(file);
  lock_release(&files_lock);

  return success;
}

int
open (const char *file)
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
read (int fd, void *buffer, unsigned size)
{
  /* STDIN */
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
write(int fd, const void *buffer, unsigned size)
{
  /* STDOUT */
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

mapid_t
mmap (int fd, const void *addr)
{
  off_t offset;
  struct spte* spte;
  struct thread *curr = thread_current();
  struct list *spt = &curr->spt;
  mapid_t mapping;;
  /* Fail conditions */
  if (filesize(fd)==0)
    return -1;
  if((uint32_t) addr % PGSIZE != 0 || addr == NULL)
    return -1;
  if (fd == 0 || fd == 1)
    return -1;

  lock_acquire(&files_lock);
  struct file *f = process_get_file(fd);
  if (!f)
  {
    lock_release(&files_lock);
    return -1;
  }
  for (offset = 0; offset < file_length(f); offset += PGSIZE)
  {
    if(spt_get_page(spt, addr+offset)!=NULL)
    {
      lock_release(&files_lock);
      return -1;
    }
  }
  curr->mapping++;
  mapping=curr->mapping;
  for (offset = 0; offset < file_length(f); offset += PGSIZE)
  {
    spte = spt_add_entry(spt, addr+offset, true, SPTE_MMAP_NOT_LOADED);
    spte->offset = offset;
    spte->mapping = mapping;
    spte->file = file_reopen(f);
  }
  lock_release(&files_lock);
  return mapping;
}

void
munmap (mapid_t mapping)
{
  spt_munmap(mapping);
}

/* Helper functions */
/* Pulls n arguments from stack and puts them into arg */
void
pull_args (struct intr_frame *f, int *arg, int n)
{
  int *ptr;
  ptr = (int *) f->esp;
  int i;
  for (i = 0; i < n; i++)
  {
    ptr++;
    check_ptr((const void *) ptr, f->esp);
    arg[i] = *ptr;
  }
}

/* Takes virtual address and uses process page directory
to get physical address  */
int
get_physical_addr(const void *vaddr, void* esp)
{
  check_ptr(vaddr,esp);
  void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
  if (!ptr)
    exit(-1);

  return (int) ptr;
}

/* Checks if pointer is valid to user virtual memory */
bool
check_ptr (const void *vaddr, void * esp)
{
  if (!is_user_vaddr(vaddr) || vaddr < USER_VADDR_BOTTOM)
      exit(-1);

  bool success=false;
  void *p = pagedir_get_page(thread_current()->pagedir, vaddr);
  if (p)
  {
    success = true;
  }
  else
  {
    if (vaddr > USER_VADDR_BOTTOM && vaddr >= esp-32)
    {
      success = spt_grow(vaddr);
    }
  }
  if (!success)
    exit(-1);
  return true;
}

/* Checks if buffer is valid for the given size */
void
check_buffer (void *buffer, unsigned size, void *esp)
{
  int i;
  char* buf_ptr = (char *) buffer;
  for (i = 0; i < size; i++)
    {
      check_ptr((const void*) buf_ptr, esp);
      buf_ptr++;
    }
}

/* Checks if the whole string is in valid memory,
    Typically used to check file name strings  */
void
check_string (const void *str, void* esp)
{
  /* If any character is not in valid memory,
      get_physical_addr throws error and exits process. */
  check_ptr(str,esp);
  while (* (char *) str != 0)
    {
      str = (char *) str + 1;
      check_ptr(str,esp);
    }
}
