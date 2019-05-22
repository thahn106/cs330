#include "filesys/cache.h"
#include "devices/disk.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/inode.h"

#define BUFFER_CACHE_SIZE 64
#define SECTORS_PER_PAGE  (PGSIZE / DISK_SECTOR_SIZE)

static struct disk *file_disk;
static struct cache cache_list[BUFFER_CACHE_SIZE];

/* Synchronization */
static struct lock cache_lock;
struct semaphore cache_sema;
static struct list job_list;
static void read_ahead_cache(void *);

struct job
{
  disk_sector_t sector;
  struct inode *inode;
  struct list_elem elem;
};

int cache_search(disk_sector_t);
int cache_get(disk_sector_t, struct inode*);
void cache_disk_read(int);
void cache_disk_write(int);
static int clock_next(void);
static int clock = 0;


void
cache_init (void)
{
  file_disk = disk_get(0,1);
  lock_init(&cache_lock);

  sema_init(&cache_sema, 0);
  list_init(&job_list);
  thread_create("CACHE_WORKER", PRI_DEFAULT, read_ahead_cache, NULL);

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
  lock_acquire(&cache_lock);
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using && cache_list[i].dirty)
    cache_disk_write(i);
  }
  lock_release(&cache_lock);
}

void
cache_evict_all(void)
{
  lock_acquire(&cache_lock);
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using)
      cache_evict(i);
  }
  lock_release(&cache_lock);
}

void
cache_evict_inode(struct inode *inode)
{
  lock_acquire(&cache_lock);
  int i;
  for (i=0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (cache_list[i].using && cache_list[i].inode == inode)
      cache_evict(i);
  }
  lock_release(&cache_lock);
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
  lock_acquire(&cache_lock);
  int index = cache_search(sector);
  if (index == -1)
    index = cache_get(sector, inode);
  lock_release(&cache_lock);
  return cache_list[index].kpage;
}

void
cache_load_next(disk_sector_t sector, struct inode *inode)
{
  struct job *job = malloc(sizeof(struct job));
  job->sector = sector;
  job->inode = inode;
  list_push_back(&job_list, &job->elem);
  sema_up(&cache_sema);
}

void
cache_set_dirty(disk_sector_t sector)
{
  lock_acquire(&cache_lock);
  int index = cache_search(sector);
  cache_list[index].dirty = true;
  lock_release(&cache_lock);
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

static void
read_ahead_cache (void *aux)
{
  struct job *job;
  while(true)
  {
    sema_down(&cache_sema);
    job = list_entry(list_pop_front(&job_list), struct job, elem);
    cache_load(job->sector, job->inode);
    free(job);
  }
}
