#include "vm/page.h"
#include <list.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

// void mmap_unload(struct spte *);

struct spte *
spt_get_page (struct list *spt, void *upage) {
  struct spte *spte;
  struct list_elem *e;
  for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
  {
      spte =  list_entry(e, struct spte, elem);
      if (spte->upage == pg_round_down(upage))
        return spte;
  }
  return NULL;
}

struct spte *
spt_add_entry (struct list *spt, void *upage, bool writable, enum spte_status status)
{
  if (spt_get_page(spt,upage)==NULL)
  {
    struct spte *spte = (struct spte *) malloc(sizeof(struct spte));
    spte->status=status;
    spte->upage=upage;
    spte->writable=writable;
    list_push_back(spt,&spte->elem);
    return spte;
  }
  return NULL;
}

bool
spt_install_page (struct list *spt, void *upage, void *kpage, bool writable, enum spte_status status)
{
  struct spte *spte = spt_add_entry(spt, upage, writable, status);
  if (spte != NULL)
  {
    update_frame_spte(kpage, spte);
    spte->kpage=kpage;
    return true;
  }
  return false;
}

void
spt_uninstall_page(struct list *spt, void *upage)
{
  struct spte *spte;
  struct list_elem *e;
  for (e = list_begin(spt); e != list_end(spt);)
  {
    spte =  list_entry(e, struct spte, elem);
    e = list_next(e);
    if (spte->upage == upage)
    {
      mmap_unload(spte);
      list_remove(e);
      free(spte);
    }
  }
}

void
spt_uninstall_all(struct list *spt)
{
  struct spte *spte;
  struct list_elem *e;
  struct list_elem *temp;
  if (list_empty(spt))
    return;
  for (e = list_begin(spt); e != list_end(spt);)
  {
    spte =  list_entry(e, struct spte, elem);
    temp = e;
    e = list_next(e);
    mmap_unload(spte);
    list_remove(temp);
    free((void *)spte);
  }
}

void
spt_munmap(mapid_t mapping)
{
  struct spte *spte;
  struct list *spt = &thread_current()->spt;
  struct list_elem *e;
  struct list_elem *temp;
  for (e = list_begin(spt); e != list_end(spt);)
  {
    spte =  list_entry(e, struct spte, elem);
    temp = e;
    e = list_next(e);
    if(spte->mapping==mapping&&(spte->status==SPTE_MMAP_LOADED||spte->status==SPTE_MMAP_NOT_LOADED))
    {
      mmap_unload(spte);
      file_close(spte->file);
      list_remove(temp);
      free((void *)spte);
    }
  }
}

bool
spt_grow(void* vaddr)
{
  void *kpage = frame_get_page(PAL_USER|PAL_ZERO);
  bool success = false;
  void *vpage = pg_round_down(vaddr);
  if (kpage!=NULL)
  {
    success = install_page(vpage,kpage, true);
    if (success)
    {
      success = spt_install_page(&thread_current()->spt,vpage,kpage,true,SPTE_MEMORY);
      if (!success)
          free(frame_find(kpage));
    }
    else
    {
      free(frame_find(kpage));
    }
  }
  return success;
}

bool
load_elf (struct spte* spte)
{
  void *kpage = frame_get_page(PAL_USER|PAL_ZERO);
  struct file *file = spte->file;
  if (kpage==NULL)
    return false;

  memset(kpage, 0, PGSIZE);
  if (spte->read_bytes>0)
  {
    if (!file_read_at(file, kpage, spte->read_bytes, spte->offset) == spte->read_bytes)
      return false;
  }
  update_frame_spte(kpage, spte);
  spte->status=SPTE_ELF_LOADED;
  if(!install_page(spte->upage, kpage, spte->writable)){
    printf("FAILED TO INSTALL PAGE\n");
    return false;
  }
  spte->kpage=kpage;
  pagedir_set_dirty(thread_current()->pagedir, spte->upage, false);
  printf("ELF LOADED at %p\n",spte->upage);
  return true;
}

bool
elf_unload(struct frame* frame)
{
  struct thread *curr = thread_current();
  struct spte *spte = frame->spte;
  if (spte->status==SPTE_ELF_LOADED)
  {
    if(spte->writable && pagedir_is_dirty(curr->pagedir, spte->upage))
      swap_out_elf(frame);
    else{
      spte->status=SPTE_ELF_NOT_LOADED;
      memset(frame->kpage, 0, PGSIZE);
      pagedir_clear_page(thread_current()->pagedir,spte->upage);
    }
    printf("ELF UNLOADED at %p\n",spte->upage);
  }
  return true;
}


bool
load_mmap (struct spte* spte)
{
  void *kpage = frame_get_page(PAL_USER|PAL_ZERO);
  struct file *file = spte->file;
  if (kpage==NULL)
    return false;

  memset(kpage, 0, PGSIZE);
  if (file_read_at(file, kpage, PGSIZE, spte->offset)!=0){
    update_frame_spte(kpage, spte);
    spte->status=SPTE_MMAP_LOADED;
    install_page(spte->upage, kpage, spte->writable);
    spte->kpage=kpage;
    pagedir_set_dirty(thread_current()->pagedir, spte->upage, false);

    return true;
  }
  // printf("load_mmap failed. offset: %d.\n", spte->offset);
  return false;
}

bool
mmap_unload(struct spte *spte)
{
  struct thread *curr = thread_current();
  if (spte->status==SPTE_MMAP_LOADED)
  {
    if(pagedir_is_dirty(curr->pagedir, spte->upage))
      file_write_at(spte->file, spte->kpage, PGSIZE, spte->offset);
  }
  return true;
}
