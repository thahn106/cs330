#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define POINTER_COUNT DISK_SECTOR_SIZE / sizeof (void*)

static disk_sector_t index_to_sector(uint32_t index, struct inode* inode);

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    disk_sector_t start;                /* First data sector. */
    disk_sector_t sector;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[124];               /* Not used. */
  };

/* On-disk inode pointer block table
  Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk_table
{
  disk_sector_t pointers[128];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

static disk_sector_t
index_to_sector(uint32_t index, struct inode *inode)
{
  // printf("looking for index: %d\n", index);
  uint32_t PC = POINTER_COUNT;
  disk_sector_t sector;
  uint32_t sector_index = index % PC;
  uint32_t table_index = index / PC;
  // printf("looking for table_index, sector_index: %u, %u\n", table_index, sector_index);
  struct inode_disk_table *top_table = malloc(sizeof(struct inode_disk_table));
  struct inode_disk_table *mid_table = malloc(sizeof(struct inode_disk_table));
  disk_read(filesys_disk, inode->data.sector, top_table);
  disk_read(filesys_disk, top_table->pointers[table_index], mid_table);
  sector = mid_table->pointers[sector_index];
  free(top_table);
  free(mid_table);
  return sector;
}


/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length){
    // printf("looking for offset: %d\n", pos);
    return index_to_sector(pos / DISK_SECTOR_SIZE, inode);
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;

    struct inode_disk_table *top_table = malloc(sizeof(struct inode_disk_table));
    struct inode_disk_table *middle_table = malloc(sizeof(struct inode_disk_table));

    if(free_map_allocate(1, &disk_inode->sector))
    {
      int tables_needed = DIV_ROUND_UP(sectors, POINTER_COUNT);
      disk_sector_t location;
      int table_count;

      for(table_count=0;table_count<tables_needed;table_count++)
      {
        if(free_map_allocate(1,&location))
          top_table->pointers[table_count] = location;
        else
        {
          disk_write(filesys_disk, disk_inode->sector, top_table);
          inode_fail_tables(disk_inode->sector, table_count);
          free(top_table);
          free(middle_table);
          return success;
        }
      }
      disk_write(filesys_disk, disk_inode->sector, top_table);
      // printf("NEW FILE-Top_table at sector: %d\n", disk_inode->sector);

      int allocated=0;
      int i;
      static char zeros[DISK_SECTOR_SIZE];
      for (i = 0; i < tables_needed; i++)
      {
        memset(middle_table, 0, sizeof(struct inode_disk_table));
        int j;
        for (j=0;j<POINTER_COUNT && j < sectors-i*POINTER_COUNT; j++)
        {
            if (free_map_allocate(1, &location))
            {
              middle_table->pointers[j] = location;
              // printf("Data at sector: %d\n", middle_table->pointers[j]);
              disk_write (filesys_disk, location, zeros);
              allocated++;
            }
            else
            {
              disk_write(filesys_disk, top_table->pointers[i], middle_table);
              inode_fail_all(disk_inode->sector, allocated);
              free(top_table);
              free(middle_table);
              return success;
            }
        }
        disk_write(filesys_disk, top_table->pointers[i], middle_table);
        // printf("Middle_table at sector: %d\n", top_table->pointers[i]);
      }
      free(top_table);
      free(middle_table);
      success = true;
    }
    disk_write (filesys_disk, sector, disk_inode);
    // printf("Finished writing, disk_inode at: %d\n", sector);


    // if (free_map_allocate (sectors, &disk_inode->start))
    //   {
    //     if (sectors > 0)
    //       {
    //         static char zeros[DISK_SECTOR_SIZE];
    //         size_t i;
    //
    //         for (i = 0; i < sectors; i++)
    //           disk_write (filesys_disk, disk_inode->start + i, zeros);
    //       }
    //     success = true;
    //   }

    free (disk_inode);
  }
  return success;
}

void
inode_fail_all(disk_sector_t table_sector, size_t size)
{
  //Bottom
  int temp;
  static struct inode_disk_table top_table;
  static struct inode_disk_table middle_table;
  disk_read(filesys_disk, table_sector, &top_table);
  for (temp = 0; temp < DIV_ROUND_UP(size, POINTER_COUNT); temp++)
  {
    disk_read(filesys_disk, top_table.pointers[temp], &middle_table);
    int j;
    for (j=0;j<POINTER_COUNT && j < size-temp*POINTER_COUNT; j++)
    {
      free_map_release(middle_table.pointers[j],1);
    }
    free_map_release(top_table.pointers[temp],1);
  }
  free_map_release(table_sector,1);
}

void
inode_fail_tables(disk_sector_t table_sector, size_t size)
{
  int temp;
  static struct inode_disk_table top_table;
  disk_read(filesys_disk, table_sector, &top_table);
  for (temp = 0; temp < size; temp++)
  {
    free_map_release(top_table.pointers[temp],1);
  }
  free_map_release(table_sector,1);
}


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  disk_read (filesys_disk, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      cache_evict_inode(inode);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          inode_fail_all(inode->data.sector, bytes_to_sectors(inode->data.length));
          free_map_release (inode->sector, 1);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // printf("reading from sector: %d\n", sector_idx);
      // if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE)
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     disk_read (filesys_disk, sector_idx, buffer + bytes_read);
      //   }
      // else
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL)
      //       {
      //         bounce = malloc (DISK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     disk_read (filesys_disk, sector_idx, bounce);
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //   }

      bounce  = cache_load(sector_idx, inode);
      memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      if (inode_left-chunk_size > 0)
        cache_load_next(byte_to_sector(inode, offset+chunk_size), inode);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  // free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  // printf("Inode_write_at called. size: %d, offset: %d\n", size, offset);
  if (inode->deny_write_cnt)
    return 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // printf("Writing to sector: %d\n", sector_idx);

      // if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE)
      //   {
      //     /* Write full sector directly to disk. */
      //     disk_write (filesys_disk, sector_idx, buffer + bytes_written);
      //   }
      // else
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL)
      //       {
      //         bounce = malloc (DISK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //
      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left)
      //       disk_read (filesys_disk, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, DISK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     disk_write (filesys_disk, sector_idx, bounce);
      //   }

      bounce  = cache_load(sector_idx, inode);
      memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      cache_set_dirty(sector_idx);
      if (inode_left-chunk_size > 0)
        cache_load_next(byte_to_sector(inode, offset+chunk_size), inode);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
