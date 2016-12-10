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
#define NUM_DIRECT 100
#define NUM_I_BLOCKS 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
// struct inode_disk
//   {
//     block_sector_t direct[NUM_DIRECT];              /* First data sector. */
//     block_sector_t indirect;
//     block_sector_t double_indirect;
//     off_t length;                        File size in bytes.
//     unsigned magic;                     /* Magic number. */
//     uint32_t unused[23];               /* Not used. */
//     int is_dir;                     /* 1 if directory, 0 if file */
//   };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
// struct inode
//   {
//     struct list_elem elem;              /* Element in inode list. */
//     block_sector_t sector;              /* Sector number of disk location. */
//     int open_cnt;                        Number of openers.
//     bool removed;                       /* True if deleted, false otherwise. */
//     int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
//     struct inode_disk data;             /* Inode content. */
//   };

struct block_list {
  block_sector_t blocks[NUM_I_BLOCKS];
};


// CHANGE THISs
static block_sector_t get_sector (const struct inode_disk *thedisk, off_t index) {
  block_sector_t toReturn;

  /**check direct**/
  if (index < NUM_DIRECT) {
    return thedisk->direct[index];
  }

  /** check indirect**/
  if (index < NUM_DIRECT + NUM_I_BLOCKS) {
    struct block_list *indirect_block;
    indirect_block = calloc(1, sizeof(struct block_list));
    block_read (fs_device, thedisk->indirect, indirect_block);
    toReturn = indirect_block->blocks[index - NUM_DIRECT];
    free(indirect_block);
    return toReturn;
  }
  /** check double indirect **/
  if (index < NUM_DIRECT + NUM_I_BLOCKS + NUM_I_BLOCKS * NUM_I_BLOCKS) {
    off_t double_ptr =  (index - NUM_DIRECT - NUM_I_BLOCKS) / NUM_I_BLOCKS;
    off_t sing_ptr = (index - NUM_DIRECT - NUM_I_BLOCKS) % NUM_I_BLOCKS;
    struct block_list* indirect_idisk;
    struct block_list** double_disk;
    indirect_idisk = calloc(1, sizeof(struct block_list));
    double_disk = calloc(1, sizeof(struct block_list));
    block_read (fs_device, thedisk->double_indirect, double_disk);
    block_read (fs_device, double_disk[double_ptr], indirect_idisk);
    toReturn = indirect_idisk->blocks[sing_ptr];
    free(indirect_idisk);
    free(double_disk);
    return toReturn;
  }
  return -1;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
  ASSERT (inode != NULL);
  if (pos < inode->data.length) {
    off_t index = pos / BLOCK_SECTOR_SIZE;
    return get_sector (&inode->data, index);
  }
  else {
    return -1;
  }
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
  list_init (&open_inodes);
}

static bool allocate_inode(struct inode_disk* disk) {
  if (disk->length == 0) {
    return true;
  }
  static char zeros[BLOCK_SECTOR_SIZE];
  int totalSectors = disk->length / BLOCK_SECTOR_SIZE;
  if (disk->length % BLOCK_SECTOR_SIZE > 0) {
    totalSectors += 1;
  }
  if (totalSectors > NUM_DIRECT + NUM_I_BLOCKS + NUM_I_BLOCKS * NUM_I_BLOCKS) {
    return false;
  }
  for (int i = 0; i < NUM_DIRECT && totalSectors > 0; i++) {
    if (disk->direct[i]==0){
      if (!free_map_allocate (1, &disk->direct[i])) {
        return false;
      }
    }
    block_write (fs_device, disk->direct[i], zeros);
    totalSectors -= 1;
  }
  if (totalSectors == 0) {
    return true;
  }
  if (!free_map_allocate (1, &disk->indirect)) {
    return false;
  }
  struct block_list* indirectBlock = calloc(1, sizeof(struct block_list));
  for (int i = 0; i < NUM_I_BLOCKS && totalSectors > 0; i++) {
    if (!free_map_allocate (1, &indirectBlock->blocks[i])) {
      return false;
    }
    block_write (fs_device, indirectBlock->blocks[i], zeros);
    totalSectors -= 1;
  }
  block_write(fs_device, disk->indirect, indirectBlock);
  free(indirectBlock);
  if (totalSectors == 0) {
    return true;
  }
  struct block_list* doubleIndirectBlock = calloc(1, sizeof(struct block_list));
  for (int i = 0; i < NUM_I_BLOCKS && totalSectors > 0; i++) {
    struct block_list* indirectBlock = calloc(1, sizeof(struct block_list));
    free_map_allocate (1, &doubleIndirectBlock->blocks[i]);
    for (int i = 0; i < NUM_I_BLOCKS && totalSectors > 0; i++) {
      if (!free_map_allocate (1, &indirectBlock->blocks[i])) {
        return false;
      }
      totalSectors -= 1;
    }
    block_write(fs_device, doubleIndirectBlock->blocks[i], indirectBlock);
    free(indirectBlock);
  }
  block_write(fs_device, disk->double_indirect, doubleIndirectBlock);
  free(doubleIndirectBlock);
  return true;
}


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir) {
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof * disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof * disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->is_dir = is_dir ? 1 : 0;
    if (allocate_inode(disk_inode)) {
      block_write (fs_device, sector, disk_inode);
      success = true;
    }
    free (disk_inode);
  }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector) {
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) {
    inode = list_entry (e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen (inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc (sizeof * inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode) {
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      int sectors_to_free = bytes_to_sectors(inode->data.length);
      struct inode_disk* idisk = &inode->data;
      for (int i = 0; i<NUM_DIRECT && sectors_to_free>0; i++){
        free_map_release (idisk->direct[i], 1);
        sectors_to_free--;
      }
      if (sectors_to_free!=0){
        struct block_list* indirect = calloc(1, sizeof(struct block_list));
        block_read(fs_device, idisk->indirect, indirect);
        for (int i = 0; i<NUM_I_BLOCKS && sectors_to_free>0; i++){
          free_map_release (indirect->blocks[i], 1);
          sectors_to_free--;
        }
        free_map_release (idisk->indirect, 1);
        free(indirect);
      }
      free_map_release (inode->sector, 1);
    }
    free (inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) {
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      block_read (fs_device, sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
         into caller's buffer. */
      if (bounce == NULL) {
        bounce = malloc (BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      block_read (fs_device, sector_idx, bounce);
      memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free (bounce);

  return bytes_read;
}

//Adds blocks to an inode that doesn't have enough allocated
bool add_blocks_inode(struct inode* inode, off_t ofs) {
  static char empty[BLOCK_SECTOR_SIZE];
  int num_to_add = ofs / BLOCK_SECTOR_SIZE;
  if (ofs % BLOCK_SECTOR_SIZE > 0) {
    num_to_add ++;
  }
  struct inode_disk* disk = &inode->data;
  for (int directNum = 0; directNum < 100 && num_to_add > 0; directNum++) {
    if (disk->direct[directNum] == 0) {
      if (!free_map_allocate (1, &disk->direct[directNum])) {
        return false;
      }
    }
    num_to_add--;
  }
  if (num_to_add == 0) {return true;}
  // check if indirect already allocated. Allocate if not
  if (disk->indirect == 0) {
    if (!free_map_allocate (1, &disk->indirect)) {
      return false;
    }
    block_write(fs_device, disk->indirect, empty);
  }

  struct block_list* indirect = calloc(1, sizeof(struct block_list));
  block_read(fs_device, disk->indirect, indirect);
  for (int indNum = 0; indNum < NUM_I_BLOCKS && num_to_add > 0; indNum++) {
    if (indirect->blocks[indNum] == 0) {
      if (!free_map_allocate (1, &indirect->blocks[indNum])) {
        return false;
      }
    }
    num_to_add--;
  }
  block_write(fs_device, disk->indirect, indirect);
  free(indirect);

  if (num_to_add == 0) {return true;}

  if (disk->double_indirect == 0) {
    if (!free_map_allocate (1, &disk->double_indirect)) {
      return false;
    }
    block_write(fs_device, disk->double_indirect, empty);
  }

  indirect = calloc(1, sizeof(struct block_list));
  struct block_list* doubleInd = calloc(1, sizeof(struct block_list));
  block_read(fs_device, disk->double_indirect, doubleInd);
  for (int indNum = 0; indNum < NUM_I_BLOCKS && num_to_add > 0; indNum++) {
    if (doubleInd->blocks[indNum] == 0) {
      if (!free_map_allocate (1, &doubleInd->blocks[indNum])) {
        return false;
      }
      block_write(fs_device, doubleInd->blocks[indNum], empty);
    }

    struct block_list* indirect = calloc(1, sizeof(struct block_list));
    block_read(fs_device,doubleInd->blocks[indNum], indirect);
    for (int sec = 0; sec < NUM_I_BLOCKS && num_to_add > 0; sec++) {
      if (indirect->blocks[sec] == 0) {
        if (!free_map_allocate (1, &indirect->blocks[sec])) {
          return false;
        }
      }
      num_to_add--;
    }
    block_write(fs_device, doubleInd->blocks[indNum], indirect);
    free(indirect);
  }
  block_write(fs_device, disk->double_indirect, doubleInd);
  return true;
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) {
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if ( byte_to_sector(inode, offset + size - 1) == -1 ) {
    if (!add_blocks_inode (inode, offset + size)) {
      return 0;
    }
    inode->data.length = offset + size;
    block_write (fs_device, inode->sector, &inode->data);
  }
  
  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      block_write (fs_device, sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc (BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
         we're writing, then we need to read in the sector
         first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        block_read (fs_device, sector_idx, bounce);
      else
        memset (bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      block_write (fs_device, sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) {
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
  return inode->data.length;
}
