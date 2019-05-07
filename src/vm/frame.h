#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"

struct frame
{
  void *kpage;
  struct spte *spte;
  struct thread *owner;
  struct list_elem elem;
};

static struct list frame_list;
static struct lock frame_lock;


void frame_init(void);
void *frame_get_page(enum palloc_flags);
void frame_free_page(void*);
struct frame *frame_find(void*);
void update_frame_spte(void*, struct spte*);
bool frame_evict(struct frame*);


#endif /* vm/frame.h */
