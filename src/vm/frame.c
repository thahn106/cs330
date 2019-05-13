#include "vm/frame.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct list frame_list;
static struct lock frame_list_lock;

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
        f->spte->using=true;
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
  {
    frame->spte=spte;
    frame->owner=thread_current();
  }
}

bool
frame_evict(struct frame *frame)
{
  enum intr_level old_level = intr_disable();
  struct spte *spte= frame->spte;
  // printf("EVICTING FRAME %p of %d.\n", spte->upage, frame->owner->tid);
  bool success=false;
  switch(spte->status)
  {
    case SPTE_MEMORY:
      // printf("SWAP_OUT %p.\n", spte->upage);
      spte->status = SPTE_SWAPPED;
      intr_set_level(old_level);
      success = swap_out(frame);
      break;
    case SPTE_ELF_LOADED:
      spte->status = SPTE_SWAPPED;
      // printf("ELF_UNLOAD %p.\n", spte->upage);
      intr_set_level(old_level);
      success = elf_unload(frame);
      break;
    case SPTE_MMAP_LOADED:
      intr_set_level(old_level);
      success = mmap_unload(spte);
      break;
    default:
      intr_set_level(old_level);
      // printf("WEIRD STATUS %d.\n", spte->status);
      break;
  }
  return success;
}
