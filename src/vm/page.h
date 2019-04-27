#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>

struct spte
{
  int status;
  void *upage;
  bool writable;
  struct list_elem elem;
};

struct spte *spt_get_page (struct list*, void*);
bool spt_install_page (struct list*, void*, void* , bool);
void spt_uninstall_page (struct list*, void*);
void spt_uninstall_all (struct list*);

#endif /* vm/page.h */
