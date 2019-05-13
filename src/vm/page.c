#include "vm/page.h"
#include <list.h>
#include <hash.h>
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


unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct spte *spte = hash_entry(e, struct spte, elem);
  return hash_int((int) spte->upage);
}

static
bool page_less (const struct hash_elem *a,
			    const struct hash_elem *b,
			    void *aux UNUSED)
{
  struct spte *sa = hash_entry(a, struct spte, elem);
  struct spte *sb = hash_entry(b, struct spte, elem);
  if (sa->upage < sb->upage)
    {
      return true;
    }
  return false;
}

static void page_destroy (struct hash_elem *e, void *aux UNUSED)
{
  struct spte *spte = hash_entry(e, struct spte, elem);
  mmap_unload(spte);
  pagedir_clear_page(thread_current()->pagedir, spte->upage);
  frame_free_page(spte->kpage);
  free(spte);
}

void
spt_init(struct hash *spt){
  hash_init(spt, page_hash, page_less, NULL);
}


// struct spte *
// spt_get_page (struct list *spt, void *upage) {
//   struct spte *spte;
//   struct list_elem *e;
//   for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
//   {
//       spte =  list_entry(e, struct spte, elem);
//       if (spte->upage == pg_round_down(upage))
//         return spte;
//   }
//   return NULL;
// }
struct spte *
spt_get_page (struct hash *spt, void *upage) {
  struct spte spte;
  spte.upage = pg_round_down(upage);
  struct hash_elem *e = hash_find(spt, &spte.elem);
  if (!e)
  {
    return NULL;
  }
  return hash_entry (e, struct spte, elem);
}

struct spte *
spt_add_entry (struct hash *spt, void *upage, bool writable, enum spte_status status)
{
  if (upage != pg_round_down(upage))
    return NULL;

  if (spt_get_page(spt,upage)==NULL)
  {
    struct spte *spte = (struct spte *) malloc(sizeof(struct spte));
    spte->status=status;
    spte->upage=upage;
    spte->writable=writable;
    spte->using=false;
    hash_insert(spt,&spte->elem);
    return spte;
  }
  return NULL;
}

bool
spt_install_page (struct hash *spt, void *upage, void *kpage, bool writable, enum spte_status status)
{
  struct spte *spte = spt_add_entry(spt, upage, writable, status);
  if (spte != NULL)
  {
    spte->using=true;
    update_frame_spte(kpage, spte);
    spte->kpage=kpage;
    return true;
  }
  return false;
}

void
spt_uninstall_page(struct hash *spt, void *upage)
{
  struct spte *spte = spt_get_page(spt, upage);
  struct hash_elem *e = &spte->elem;
  if (spte)
  {
    mmap_unload(spte);
    hash_delete(spt,e);
    free(spte);
  }
}

void
spt_uninstall_all(struct hash *spt)
{
  hash_destroy (spt, page_destroy);
}

void
spt_munmap(mapid_t mapping)
{
  struct spte *spte;
  struct list *mlist = &thread_current()->mmap_list;
  struct hash *spt = &thread_current()->spt;
  struct list_elem *e;
  struct list_elem *temp;
  for (e = list_begin(mlist); e != list_end(mlist);)
  {
    spte =  list_entry(e, struct spte, mmap_elem);
    temp = e;
    e = list_next(e);
    if(spte->mapping==mapping && (spte->status==SPTE_MMAP_LOADED||spte->status==SPTE_MMAP_NOT_LOADED))
    {
      mmap_unload(spte);
      file_close(spte->file);
      list_remove(temp);
      hash_delete(spt, &spte->elem);
      free((void *)spte);
    }
  }
}

bool
spt_grow(void* vaddr)
{
  if ( (size_t) (PHYS_BASE - pg_round_down(vaddr)) > (1<<23))
    {
      return false;
    }
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
load_elf (struct spte* spte, void *kpage)
{
  // void* kpage =frame_get_page(PAL_USER|PAL_ZERO);
  struct file *file = spte->file;
  if (kpage==NULL)
    return false;

  memset(kpage, 0, PGSIZE);
  if (spte->read_bytes>0)
  {
    if (!file_read_at(file, kpage, spte->read_bytes, spte->offset) == spte->read_bytes)
      return false;
  }
  if(!install_page(spte->upage, kpage, spte->writable)){
    // printf("FAILED TO INSTALL PAGE\n");
    return false;
  }
  update_frame_spte(kpage, spte);
  spte->status = SPTE_ELF_LOADED;
  spte->kpage = kpage;
  pagedir_set_dirty(thread_current()->pagedir, spte->upage, false);
  // printf("ELF LOADED at %p\n",spte->upage);
  return true;
}

bool
elf_unload(struct frame* frame)
{
  struct spte *spte = frame->spte;
  // printf("ELF %p status %d.\n", spte->upage, spte->status);
  if(spte->writable && pagedir_is_dirty(frame->owner->pagedir, spte->upage))
  {
    frame->spte->status = SPTE_ELF_SWAPPED;
    // printf("SWAPPING OUT ELF %p status %d.\n",frame->spte->upage, frame->spte->status);
    swap_out_elf(frame);
    // pagedir_clear_page(frame->owner->pagedir,spte->upage);
    // printf("ELF %p status %d.\n",frame->spte->upage, frame->spte->status);
  }
  else{
    // printf("UNLOADING ELF %p status %d.\n",frame->spte->upage, frame->spte->status);
    spte->status=SPTE_ELF_NOT_LOADED;
    memset(frame->kpage, 0, PGSIZE);
    pagedir_clear_page(frame->owner->pagedir,spte->upage);
    // printf("ELF %p status %d.\n",spte->upage, spte->status);
  }
  // printf("ELF %p status %d.\n", spte->upage, spte->status);
  return true;
}


bool
load_mmap (struct spte *spte, void *kpage)
{
  // void* kpage =frame_get_page(PAL_USER|PAL_ZERO);
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
