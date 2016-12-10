#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  block_sector_t inode_sector = 0;
  // struct dir *dir = dir_open_root ();
  char directory[strlen(name)+1], file[strlen(name)+1];
  separate_path_and_file(name, directory, file);
  struct dir* dir = dir_open_path(directory);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file, inode_sector, is_dir));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

// CHANGE THIS
/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  // struct dir *dir = dir_open_root ();
  // struct inode *inode = NULL;

  if(strlen(name) == 0) {
    return NULL;
  }

  char direc[strlen(name) + 1], file_name[strlen(name) + 1];
  separate_path_and_file(name, direc, file_name);
  struct dir* dir = dir_open_path(direc);
  struct inode* inode = NULL;

  /* Return NULL if dir was removed */
  if(dir == NULL) {
    return NULL;
  }

  if(strlen(file_name) > 0) {
    dir_lookup(dir, file_name, &inode);
    dir_close(dir);
  }
  /* If file_name is empty, get the inode of the opened directory */
  else {
    inode = dir_get_inode(dir);
  }

  if(inode == NULL || inode->removed) {
    return NULL;
  }

  return file_open(inode);

  // if (dir != NULL)
  //   dir_lookup (dir, name, &inode);
  // dir_close (dir);

  // return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  // struct dir *dir = dir_open_root ();
  // bool success = dir != NULL && dir_remove (dir, name);
  // dir_close (dir); 

  char direc[strlen(name)], file_name[strlen(name)];
  separate_path_and_file(name, direc, file_name);
  struct dir* dir = dir_open_path(direc);

  bool success = (dir != NULL && dir_remove(dir, file_name));
  dir_close(dir);

  return success;
}

/* Change curr_work_dir for the current thread */
bool
filesys_chdir(const char* name) {
  struct dir* new_dir = dir_open_path(name);
  /* Make sure there exists a directory by NAME */
  if(new_dir == NULL) {
    return false;
  } 

  /* Make sure the directory wasn't removed */
  else if(new_dir->inode->removed) {
    return false;
  }
  struct thread* t = thread_current();
  dir_close(t->curr_work_dir);
  t->curr_work_dir = new_dir;
  return true;
}


/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
