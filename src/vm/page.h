#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "filesys/inode.h"
#include "threads/thread.h"

enum spte_status
{
  SPTE_MEMORY,
  SPTE_SWAPPED,
  SPTE_ELF_LOADED,
  SPTE_ELF_NOT_LOADED,
  SPTE_MMAP_LOADED,
  SPTE_MMAP_NOT_LOADED,
};

struct spte
{
  enum spte_status status;
  void *upage;
  bool writable;

  int fd;
  off_t offset;
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
bool load_mmap(struct spte*);

#endif /* vm/page.h */
