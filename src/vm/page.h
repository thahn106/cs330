#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "filesys/inode.h"
#include "threads/thread.h"
#include "vm/frame.h"

enum spte_status
{
  SPTE_MEMORY,
  SPTE_SWAPPED,
  SPTE_ELF_LOADED,
  SPTE_ELF_NOT_LOADED,
  SPTE_ELF_SWAPPED,
  SPTE_MMAP_LOADED,
  SPTE_MMAP_NOT_LOADED,
};

struct spte
{
  enum spte_status status;
  void *upage;
  void *kpage;
  bool writable;

  uint32_t read_bytes;
  uint32_t zero_bytes;
  /* Swap space */
  size_t index;

  /* FILE / MMAP */
  off_t offset;
  struct file *file;
  /* MMAP only */
  mapid_t mapping;


  struct list_elem elem;
};

struct spte *spt_get_page (struct list*, void*);
struct spte *spt_add_entry (struct list*, void*, bool, enum spte_status);
bool spt_install_page (struct list*, void*, void* , bool, enum spte_status);
void spt_uninstall_page (struct list*, void*);
void spt_uninstall_all (struct list*);
void spt_munmap(mapid_t);

bool spt_grow(void*);

bool load_elf(struct spte*);
bool unload_elf(struct frame*);

bool load_mmap(struct spte*);
bool mmap_unload(struct spte*);
#endif /* vm/page.h */
