#include "vm/page.h"
#include <list.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"

struct spte *
spt_get_page (struct list *spt, void *upage) {
  struct spte *spte;
  struct list_elem *e;
  for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
  {
      spte =  list_entry(e, struct spte, elem);
      if (spte->upage == upage)
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
    list_remove(temp);
    free((void *)spte);
  }
}

void
spt_munmap(mapid_t mapping)
{
  struct spte *spte;
  struct thread *curr = thread_current();
  struct list *spt = &curr->spt;
  struct file *file;
  struct list_elem *e;
  struct list_elem *temp;
  for (e = list_begin(spt); e != list_end(spt);)
  {
    spte =  list_entry(e, struct spte, elem);
    temp = e;
    e = list_next(e);
    if(spte->mapping==mapping)
    {
      if (spte->status==SPTE_MMAP_LOADED)
      {
        if(pagedir_is_dirty(curr->pagedir, spte->upage))
        {
          file = process_get_file(spte->fd);
          file_write_at(file, spte->kpage, PGSIZE, spte->offset);
        }
        list_remove(temp);
        free((void *)spte);
      }
      if(spte->status==SPTE_MMAP_NOT_LOADED)
      {
        list_remove(temp);
        free((void *)spte);
      }
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
load_mmap (struct spte* spte)
{
  void *kpage = frame_get_page(PAL_USER|PAL_ZERO);
  struct file *file = process_get_file(spte->fd);
  if (kpage==NULL)
    return false;

  memset(kpage, 0, PGSIZE);
  if (file_read_at(file, kpage, PGSIZE, spte->offset)!=0){
    frame_find(kpage)->spte=spte;
    spte->status=SPTE_MMAP_LOADED;
    install_page(spte->upage, kpage, spte->writable);
    spte->kpage=kpage;
    pagedir_set_dirty(thread_current()->pagedir, spte->upage, 0);

    return true;
  }
  //Free Frame TODO
  return false;
}
