#include "filesys/cache.h"
#include "devices/disk.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/inode.h"

#define BUFFER_CACHE_SIZE 64
#define SECTORS_PER_PAGE  (PGSIZE / DISK_SECTOR_SIZE)

static struct disk *file_disk;
static struct cache cache_list[BUFFER_CACHE_SIZE];

int cache_get(disk_sector_t, struct inode*);
void cache_disk_read(int);
void cache_disk_write(int);
static int clock_next(void);
static int clock = 0;


void
cache_init (void)
{
  file_disk = disk_get(0,1);
  int i, j;
  for (i = 0;i< BUFFER_CACHE_SIZE/ SECTORS_PER_PAGE; i++)
  {
    void *kpage = palloc_get_page(PAL_ZERO);
    for (j=0; j < SECTORS_PER_PAGE; j++)
    {
        cache_list[i*SECTORS_PER_PAGE + j].kpage = kpage + j * DISK_SECTOR_SIZE;
        ASSERT(cache_list[i*SECTORS_PER_PAGE + j].kpage!= NULL);
    }
  }
}

void
cache_flush(void)
{
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using && cache_list[i].dirty)
    cache_disk_write(i);
  }
}

void
cache_evict_all(void)
{
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using)
      cache_evict(i);
  }
}

void
cache_evict_inode(struct inode *inode)
{
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using && cache_list[i].inode == inode)
      cache_evict(i);
  }
}

/* Finds if a sector is in cache, returns -1 if not found. */
int
cache_search(disk_sector_t index)
{
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using && cache_list[i].sector ==  index)
    return i;
  }
  return -1;
}

void *
cache_load(disk_sector_t sector, struct inode *inode)
{
  int index = cache_search(sector);
  if (index == -1)
    index = cache_get(sector, inode);
  return cache_list[index].kpage;
}

void
cache_set_dirty(disk_sector_t sector)
{
  int index = cache_search(sector);
  cache_list[index].dirty = true;
}


/* Find empty cache, evicting one if needed */
int
cache_get(disk_sector_t sector,struct inode *inode)
{
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (!cache_list[i].using)
    {
      cache_list[i].sector = sector;
      cache_disk_read(i);
      cache_list[i].using = true;
      cache_list[i].inode = inode;
      return i;
    }
  }
  /* No empty cache slot */
  int evict = clock_next();
  while (true)
  {
    if (!cache_list[evict].clock)
    {
      cache_evict(evict);
      cache_list[evict].sector = sector;
      cache_disk_read(evict);
      cache_list[evict].using = true;
      cache_list[evict].inode = inode;
      return evict;
    }
    cache_list[evict].clock = false;
    evict = clock_next();
  }
}


void
cache_evict(int index)
{
  if (cache_list[index].dirty)
    cache_disk_write(index);
  cache_list[index].using = false;
  cache_list[index].sector = -1;
  memset(cache_list[index].kpage, 0, DISK_SECTOR_SIZE);
}


void
cache_disk_read(int index)
{
  // printf("Index: %d, address: %p.\n", index, cache_list[index].kpage);
  disk_read(file_disk, cache_list[index].sector, cache_list[index].kpage);
  cache_list[index].dirty = false;
  cache_list[index].clock = true;
}

void
cache_disk_write(int index)
{
  disk_write(file_disk, cache_list[index].sector, cache_list[index].kpage);
  cache_list[index].dirty = false;
}

int
clock_next(void){
  clock++;
  clock = clock % 64;
  return clock;
}
