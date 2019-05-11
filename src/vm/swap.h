#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/page.h"
#include "vm/frame.h"

void swap_init (void);

bool swap_in (struct spte*, void*);
bool swap_out (struct frame*);
bool swap_in_elf (struct spte*, void*);
bool swap_out_elf (struct frame*);

void read_from_disk (struct frame*, int index);
void write_to_disk (struct frame*, int index);

#endif /* vm/swap.h */
