#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/disk.h"

struct cache
{
  void *kpage;
  bool using;
  bool dirty;
  bool clock;
  disk_sector_t sector;
};

void cache_init(void);
void cache_flush(void);
int cache_search(disk_sector_t);
bool cache_get(disk_sector_t);


#endif /* filesys/cache.h */
