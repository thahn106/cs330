#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

void
frame_init(void)
{
  list_init(&frame_list);
  lock_init(&frame_list_lock);
}

void
*frame_get_page(enum palloc_flags flag)
{
  void *p = NULL;
  bool success = false;
  struct frame *f;
  lock_acquire(&frame_list_lock);
  p = palloc_get_page(flag);

  /* If full */
  if (p==NULL){
    while(!success){
      f = list_entry(list_pop_front(&frame_list), struct frame, elem);
      if (!f->spte->using)
      {
        lock_acquire(&f->lock);
        success = frame_evict(f);
        if (!success)
          lock_release(&f->lock);
      }
      else{
        list_push_back(&frame_list, &f->elem);
      }
    }
  }
  else{
    /* Make new frame */
    f = (struct frame *) malloc(sizeof(struct frame));
    f->kpage = p;
    lock_init(&f->lock);
    lock_acquire(&f->lock);
    success = true;
  }
  if (success){
    p = f->kpage;
    f->owner = thread_current();
    list_push_back(&frame_list, &f->elem);
  }
  else{
    PANIC("FAILED TO GET FRAME\n");
  }
  lock_release(&f->lock);
  lock_release(&frame_list_lock);
  return p;
}

void
frame_free_page(void *kpage)
{
  struct frame *f;

  lock_acquire(&frame_list_lock);

  struct list_elem *e;
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e))
  {
    f = list_entry(e, struct frame, elem);
    if (f->kpage==kpage)
    {
      list_remove(e);
      palloc_free_page(kpage);
      free(f);
      lock_release(&frame_list_lock);
      return;
    }
  }
  /* Invalid kpage pointer */
  lock_release(&frame_list_lock);
}

struct frame*
frame_find(void* kpage)
{
  struct frame *f;
  lock_acquire(&frame_list_lock);
  struct list_elem *e;
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e))
  {
    f = list_entry(e, struct frame, elem);
    if (f->kpage==kpage)
    {
      lock_release(&frame_list_lock);
      return f;
    }
  }
  /* Invalid kpage pointer */
  lock_release(&frame_list_lock);
  return NULL;
}

void
update_frame_spte(void* kpage, struct spte* spte)
{
  struct frame* frame = frame_find(kpage);
  if (frame!=NULL)
    frame->spte=spte;
}

bool
frame_evict(struct frame *frame)
{
  struct spte *spte= frame->spte;
  // printf("EVICTING FRAME %p.\n", spte->upage);
  bool success=false;
  switch(spte->status)
  {
    case SPTE_MEMORY:
      // printf("SWAP_OUT %p.\n", spte->upage);
      success = swap_out(frame);
      break;
    case SPTE_ELF_LOADED:
      // printf("SWAP_OUT_ELF %p.\n", spte->upage);
      success = elf_unload(frame);
      if (success){
        int t= spte->status;
        // printf("SWAP_OUT_ELF %p successful at frame_evict with status %d.\n", spte->upage, t);
      }
      // else printf("SWAP_OUT_ELF %p unsuccessful.\n", spte->upage);
      break;
    case SPTE_MMAP_LOADED:
      // printf("SWAP_OUT_MMAP %p.\n", spte->upage);
      success = mmap_unload(spte);
      break;
    default:
      // printf("WEIRD STATUS %d.\n", spte->status);
      break;
  }
  return success;
}
