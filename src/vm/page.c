#include "vm/page.h"
#include <list.h>
#include "threads/malloc.h"
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

bool
spt_install_page (struct list *spt, void *upage, void *kpage, bool rw)
{
  if (spt_get_page(spt,upage)==NULL)
  {
    struct spte *spte = (struct spte *) malloc(sizeof(struct spte));
    spte->upage=upage;
    spte->writable=rw;
    list_push_back(spt,&spte->elem);
    update_frame_spte(kpage, spte);
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
