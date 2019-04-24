#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

void
frame_init(void)
{
  list_init(&frame_list);
  lock_init(&frame_lock);
}

void
*frame_get_page(enum palloc_flags flag)
{
  void *p = NULL;
  lock_acquire(&frame_lock);
  p = palloc_get_page(flag);

  /* If full */
  if (p==NULL){
    lock_release(&frame_lock);
    PANIC("Page table full");
  }

  update_frame(p);
  lock_release(&frame_lock);
  return p;
}

void
frame_free_page(void *kpage)
{
  struct frame *f;

  lock_acquire(&frame_lock);

  struct list_elem *e;
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e))
  {
    f = list_entry(e, struct frame, elem);
    if (f->kpage==kpage){
      list_remove(e);
      palloc_free_page(kpage);
      free(f);
      lock_release(&frame_lock);
      return;
    }
  }
  /* Invalid kpage pointer */
  lock_release(&frame_lock);
}

void
update_frame(void *p)
{
  struct frame *f = (struct frame *) malloc(sizeof(struct frame));
  f->kpage = p;
  f->owner = thread_current();

  list_push_back(&frame_list, &f->elem);
}
