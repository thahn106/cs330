#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/disk.h"
#include "filesys/inode.h"

struct cache
{
  void *kpage;
  bool using;
  bool dirty;
  bool clock;
  struct inode *inode;
  disk_sector_t sector;
};

void cache_init(void);
void cache_flush(void);
void cache_evict_all(void);
void cache_evict_inode(struct inode*);
int cache_search(disk_sector_t);
void *cache_load(disk_sector_t, struct inode*);
void cache_set_dirty(disk_sector_t);


#endif /* filesys/cache.h */
