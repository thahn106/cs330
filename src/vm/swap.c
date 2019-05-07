#include "vm/swap.h"
#include <bitmap.h>
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"

#define SECTORS_PER_PAGE (PGSIZE/DISK_SECTOR_SIZE)

/* The swap device */
static struct disk *swap_device;

/* Tracks in-use and free swap slots */
static struct bitmap *swap_table;

/* Protects swap_table */
static struct lock swap_lock;

/*
 * Initialize swap_device, swap_table, and swap_lock.
 */
void
swap_init (void)
{
  swap_device = disk_get(1,1);
  swap_table = bitmap_create(disk_size(swap_device)/SECTORS_PER_PAGE);
  bitmap_set_all (swap_table, false);
  printf("swap initialized.\n");
  lock_init(&swap_lock);
}

/*
 * Reclaim a frame from swap device.
 * 1. Check that the page has been already evicted.
 * 2. You will want to evict an already existing frame
 * to make space to read from the disk to cache.
 * 3. Re-link the new frame with the corresponding supplementary
 * page table entry.
 * 4. Do NOT create a new supplementray page table entry. Use the
 * already existing one.
 * 5. Use helper function read_from_disk in order to read the contents
 * of the disk into the frame.
 */
bool
swap_in (struct spte *spte)
{
  bool success = false;
  size_t index = spte->index;
  struct frame *frame = frame_get_page(PAL_USER|PAL_ZERO);

  /* Find free sector and swap out */
  lock_acquire(&swap_lock);

  read_from_disk(frame, index);

  bitmap_set(swap_table, index, false);
  update_frame_spte(frame,spte);
  install_page(spte->upage, frame->kpage, true);
  spte->status = SPTE_MEMORY;

  success=true;
  lock_release(&swap_lock);

  return success;
}

/*
 * Evict a frame to swap device.
 * 1. Choose the frame you want to evict.
 * (Ex. Least Recently Used policy -> Compare the timestamps when each
 * frame is last accessed)
 * 2. Evict the frame. Unlink the frame from the supplementray page table entry
 * Remove the frame from the frame table after freeing the frame with
 * pagedir_clear_page.
 * 3. Do NOT delete the supplementary page table entry. The process
 * should have the illusion that they still have the page allocated to
 * them.
 * 4. Find a free block to write you data. Use swap table to get track
 * of in-use and free swap slots.
 */
bool
swap_out (struct frame *frame)
{
  bool success=false;
  size_t index;

  /* Find free sector and swap out */
  lock_acquire(&swap_lock);
  index = bitmap_scan(swap_table, 0, 1, false);
  if (index == BITMAP_ERROR)
    return success;

  write_to_disk(frame, index);

  frame->spte->status = SPTE_SWAPPED;
  bitmap_set(swap_table, index, true);
  pagedir_clear_page(thread_current()->pagedir,frame->spte->upage);

  success=true;
  lock_release(&swap_lock);

  return success;
}

bool
swap_in_elf (struct spte *spte)
{
  bool success = false;
  size_t index = spte->index;
  struct frame *frame = frame_get_page(PAL_USER|PAL_ZERO);

  /* Find free sector and swap out */
  lock_acquire(&swap_lock);

  read_from_disk(frame, index);
  bitmap_set(swap_table, index, false);
  update_frame_spte(frame,spte);
  install_page(spte->upage, frame->kpage, true);
  spte->status = SPTE_ELF_LOADED;

  success=true;
  lock_release(&swap_lock);

  return success;
}

bool
swap_out_elf (struct frame *frame)
{
  bool success=false;
  size_t index;

  /* Find free sector and swap out */
  lock_acquire(&swap_lock);
  index = bitmap_scan(swap_table, 0, 1, false);
  if (index == BITMAP_ERROR)
    return success;

  write_to_disk(frame, index);

  frame->spte->status = SPTE_ELF_SWAPPED;
  frame->spte->index=index;

  bitmap_set(swap_table, index, true);
  pagedir_clear_page(thread_current()->pagedir,frame->spte->upage);

  success=true;
  lock_release(&swap_lock);

  return success;
}


/*
 * Read data from swap device to frame.
 * Look at device/disk.c
 */
void read_from_disk (struct frame *frame, int index)
{
  int i;
  for (i = 0; i < SECTORS_PER_PAGE; i++)
  {
    disk_read(swap_device, index*SECTORS_PER_PAGE + i, frame->kpage+i*DISK_SECTOR_SIZE);
  }
}

/* Write data to swap device from frame */
void write_to_disk (struct frame *frame, int index)
{
  int i;
  for (i = 0; i < SECTORS_PER_PAGE; i++)
  {
    disk_write(swap_device, index*SECTORS_PER_PAGE + i, frame->kpage+i*DISK_SECTOR_SIZE);
  }
}
