#include "filesys/cache.h"
#include "devices/disk.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

#define BUFFER_CACHE_SIZE 64
#define SECTORS_PER_PAGE  (PGSIZE / DISK_SECTOR_SIZE)


static struct disk *file_disk;
struct cache cache_list[BUFFER_CACHE_SIZE];





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
        cache_list[i*SECTORS_PER_PAGE + j].kpage = kpage+j*DISK_SECTOR_SIZE;
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

/* Find empty cache, evicting one if needed */
bool
cache_get(disk_sector_t sector)
{
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (!cache_list[i].using)
    {
      cache_list[i].sector = sector;
      cache_disk_read(i);
      cache_list[i].using = true;
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
      cache_list[i].sector = sector;
      cache_disk_read(i);
      cache_list[i].using = true;
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
