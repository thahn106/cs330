#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <hash.h>
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

  bool using;

  uint32_t read_bytes;
  uint32_t zero_bytes;
  /* Swap space */
  size_t index;

  /* FILE / MMAP */
  off_t offset;
  struct file *file;
  /* MMAP only */
  mapid_t mapping;
  struct list_elem mmap_elem;

  // struct list_elem elem;
  struct hash_elem elem;
};


void spt_init(struct hash*);

// struct spte *spt_get_page (struct list*, void*);
// struct spte *spt_add_entry (struct list*, void*, bool, enum spte_status);
// bool spt_install_page (struct list*, void*, void* , bool, enum spte_status);
struct spte *spt_get_page (struct hash*, void*);
struct spte *spt_add_entry (struct hash*, void*, bool, enum spte_status);
bool spt_install_page (struct hash*, void*, void* , bool, enum spte_status);

void spt_uninstall_page (struct hash*, void*);
void spt_uninstall_all (struct hash*);
void spt_munmap(mapid_t);

bool spt_grow(void*);

bool load_elf(struct spte*, void*);
bool unload_elf(struct frame*);

bool load_mmap(struct spte*, void*);
bool mmap_unload(struct spte*);
#endif /* vm/page.h */
