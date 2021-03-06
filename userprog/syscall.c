#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "filesys/inode.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static struct lock l;
static struct lock file_lock;

static void syscall_handler (struct intr_frame *);
static int memread_helper(void* addr, void* dst, size_t bytes);
static struct f_desc* get_fd_struct(struct thread* t, int fd);
void close_and_remove_file(int fd);

void
syscall_init (void) 
{
	lock_init(&l);
	lock_init(&file_lock);
  	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

int chillPtr(void* ptr){
	if (ptr==NULL || ptr >= PHYS_BASE){
		return 0;
	}
	if (pagedir_get_page(thread_current()->pagedir, ptr) == NULL){
		return 0;
	}
	if(is_kernel_vaddr(ptr)) {
		return 0;
	}
	return 1;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	if (!chillPtr(f->esp)){
		exit(-1);
	}
	int* sp = f->esp;
	int syscall_num = *sp;
	if (!(chillPtr(sp)&&chillPtr(sp+1)&&chillPtr(sp+2)&&chillPtr(sp+3))) {
		exit(-1);
	}

	switch(syscall_num) {
		case SYS_HALT:
			halt();
			break;

		case SYS_EXIT:
			exit(*(sp+1));
			break;

		case SYS_EXEC:
			f->eax = exec((char *)*(sp+1));
			break;

		case SYS_WAIT:
			f->eax=wait(*(pid_t*) (sp+1));
			break;

		case SYS_CREATE:
			f->eax= create(*((char**) (sp+1)), *(unsigned*)(sp+2));
			break;

		case SYS_REMOVE:
			f->eax=remove(*(char**) (sp+1));
			break;

		case SYS_OPEN:
			f->eax=open(*((char**) (sp+1)));
			break;

		case SYS_FILESIZE:
			f->eax=filesize(*(int*)(sp+1));
			break;

		case SYS_READ:
			f->eax=read(*(int*)(sp+1), *(void**) (sp+2),*(unsigned*)(sp+3));
			break;

		case SYS_WRITE:
			f->eax=write(*(int*)(sp+1), *(void**) (sp+2),*(unsigned*)(sp+3));
			break;

		case SYS_SEEK:
			seek(*(int*)(sp+1),*(unsigned*)(sp+2));
			break;

		case SYS_TELL:
			f->eax=tell(*(int*)(sp+1));
			break;

		case SYS_CLOSE:
			close(*(int*)(sp+1));
			break;

		case SYS_CHDIR:
		{
			const char** name;
			if(!chillPtr(f->esp + 4)) {
				exit(-1);
			}
			/* Get the specified name from the interrupt frame */
			// memread_helper(f->esp + 4, &name, sizeof(name));
			name = f->esp + 4;
			f->eax = chdir(*name);
			break;
		}

		case SYS_MKDIR:
		{
			const char** name;
			if(!chillPtr(f->esp + 4)) {
				exit(-1);
			}
			/* Get the specified name from the interrupt frame */
			// memread_helper(f->esp + 4, &name, sizeof(name));
			name = f->esp + 4;
			f->eax = mkdir(*name);
			break;
		}

		case SYS_READDIR:
		{
			int fd;
			char** name;
			if(!chillPtr(f->esp + 4) || !chillPtr(f->esp + 8)) {
				exit(-1);
			}
			/* Get the specified name from the interrupt frame */
			// memread_helper(f->esp + 4, &fd, sizeof(fd));
			name = f->esp + 8;
			/* Get the specified file descriptor from the interrupt frame */
			// memread_helper(f->esp + 8, &name, sizeof(name));
			fd = *((int*)(f->esp + 4));
			f->eax = readdir(fd, *name);
			break;
		}

		case SYS_ISDIR:
		{
			int fd;
			if(!chillPtr(f->esp + 4)) {
				exit(-1);
			}
			/* Get the specified file descriptor from the interrupt frame */
			// memread_helper(f->esp + 4, &fd, sizeof(fd));
			fd = *((int*)(f->esp + 4));
			f->eax = isdir(fd);
			break;
		}

		case SYS_INUMBER:
		{
			int fd;
			if(!chillPtr(f->esp + 4)) {
				exit(-1);
			}
			/* Get the specified file descriptor from the interrupt frame */
			// memread_helper(f->esp + 4, &fd, sizeof(fd));
			fd = *((int*)(f->esp + 4));
			f->eax = inumber(fd);
			break;
		}
	}
}

/* Additional filesys sycalls */
bool 
chdir(const char* dir) {
	bool result;
	if(!chillPtr(dir)) {
		exit(-1);
	}
	lock_acquire(&l);
	result = filesys_chdir(dir);
	lock_release(&l);
	return result;
}

bool
mkdir(const char* dir) {
	bool result;
	if(!chillPtr(dir)) {
		exit(-1);
	}
	if(strcmp(dir, "") == 0) {
		return false;
	}
	lock_acquire(&l);
	result = filesys_create(dir, 0, true);
	lock_release(&l);
	return result;
}

bool 
readdir(int fd, char* name) {
	lock_acquire(&l);
	struct f_desc* file_d = get_fd_struct(thread_current(), fd);
	struct inode* my_inode = file_get_inode(file_d->file);
	if(my_inode == NULL) {
		lock_release(&l);
		return false;
	}

	if(!my_inode->data.is_dir) {
		lock_release(&l);
		return false;
	}
	bool result = dir_readdir(file_d->dir, name);
	lock_release(&l);
	return result;
}

bool
isdir(int fd) {
	lock_acquire(&l);
	struct f_desc* file_d = thread_current()->fd_table[fd];
	struct inode* my_inode = file_get_inode(file_d->file);
	if(my_inode == NULL) {
		lock_release(&l);
		return false;
	}
	bool result = my_inode->data.is_dir;
	lock_release(&l);
	return result;
}

int 
inumber(int fd) {
	lock_acquire(&l);
	struct f_desc* file_d = thread_current()->fd_table[fd];
	struct inode* inode = file_get_inode(file_d->file);
	int result = inode_get_inumber(inode);
	lock_release(&l);
	return (int) result;
}

/* All-purpose syscalls */
void halt() {
	shutdown_power_off();
}

void exit(int status) {
	 
	struct thread* cur = thread_current();
	printf("%s: exit(%d)\n", cur->name, status);
	cur->exitCode = status;
	cur->called_exit = true;
	thread_exit();
}

pid_t exec(const char* cmd_line) {
	lock_acquire(&l);

	if(!chillPtr(cmd_line)) {
		lock_release(&l);
		return -1;
	}

	pid_t pid = process_execute(cmd_line);

	if(pid == TID_ERROR) {
		lock_release(&l);
		return pid;
	}

	// Check if current thread had loading error
	if(!thread_current()->load_success) {
		lock_release(&l);
		return -1;
	}

	lock_release(&l);
	return pid;
}

int wait(pid_t pid) {	
	int toReturn = process_wait(pid);
	return toReturn;
}

bool create(const char* file, unsigned initial_size) {
	lock_acquire(&l);

	if(file == NULL || !chillPtr(file)) {
		lock_release(&l);
		exit(-1);
	}

	bool success = filesys_create(file, initial_size, false);

	lock_release(&l);
	return success;
}

bool remove(const char* file) {
	lock_acquire(&l);

	if(!chillPtr(file)) {
		lock_release(&l);
		exit(-1);
	}

	bool success = filesys_remove(file);

	lock_release(&l);
	return success;
}

int open(const char* file) {
	lock_acquire(&l);

	if(!chillPtr(file)) {
		lock_release(&l);
		exit(-1);
	}	

	struct thread* current = thread_current();
	struct f_desc* file_desc = palloc_get_page(0);
	if(file_desc == NULL) {
		return -1;
	} 
	struct file* f = filesys_open(file);

	if(!strcmp(thread_current()->name, file)) {
		file_deny_write(f);
	} else if(f == NULL) {
		lock_release(&l);
		return -1;	// ALTERED FROM "RETURN -1" to "EXIT(-1)"
	}

	struct inode* inode = file_get_inode(f);
	if(inode != NULL && inode->data.is_dir) {
		file_desc->dir = dir_open(inode_reopen(inode));
		file_desc->is_dir = true;
	}

	struct list* open_file_list = &current->open_file_list;
	f->fd = current->fd;
	current->fd++;
	list_push_back(open_file_list, &f->file_elem);
	file_desc->fd = f->fd;
	file_desc->file = f;
	file_desc->is_dir = false;
	current->fd_table[f->fd] = file_desc;

	lock_release(&l);
	return file_desc->fd;
}

int filesize(int fd) {
	lock_acquire(&l);
	struct file* f = get_file(fd);
	lock_release(&l);
	return file_length(f);
}

int read(int fd, void* buffer, unsigned size) {
	lock_acquire(&l);

	if(!chillPtr(buffer)) {
		lock_release(&l);
		exit(-1);
	} else if(fd == 0) {
		lock_release(&l);
		return input_getc();
	} else if(fd == 1) {
		lock_release(&l);
		return -1;
	} else if(fd < 0 || fd > thread_current()->fd - 1) {
		lock_release(&l);
		return -1;
	}

	struct file* f = get_file(fd);

	if(f == NULL) {
		lock_release(&l);
		return -1;
	}

	off_t bytes = file_length(f) - file_tell(f);

	if(bytes < 0) {
		lock_release(&l);

		return 0;
	} else if( size > (unsigned)bytes) {
		size = bytes;
	}

	off_t bytes_read = file_read(f, buffer, size);

	if((unsigned)bytes_read < size) {
		lock_release(&l);
		return -1;
	}

	lock_release(&l);

	return bytes_read;
}

int write (int fd, const void *buffer, unsigned size) {
	lock_acquire(&l);
	if(!chillPtr(buffer)) {
		lock_release(&l);
		exit(-1);
	}
	struct thread* cur = thread_current();
	struct f_desc* desc = cur->fd_table[fd];
	if (fd > 2 && desc->file->inode->data.is_dir!=false){
		lock_release(&l);
		return -1;
	}
	if(fd == 0) {
		lock_release(&l);
		return 0;
	}
	else if (fd == 1) {
		putbuf(buffer, size);
		lock_release(&l);
		return size;
	}
	else if(fd < 0 || fd > thread_current()->fd - 1) {
		lock_release(&l);
		return 0;
	}
	else {
		struct file* f = get_file(fd);

		if(f == NULL || f->deny_write) {
			lock_release(&l);
			return 0;
		}

		off_t bytes_written = file_write(f, buffer, size);

		lock_release(&l);
		return bytes_written;
	}
}

void seek (int fd, unsigned position) {
	lock_acquire(&l);
	struct file* f = get_file(fd);
	file_seek(f, position);
	lock_release(&l);
}

unsigned tell (int fd) {
	lock_acquire(&l);
	struct file* f = get_file(fd);
	unsigned offset = file_tell(f);
	lock_release(&l);
	return offset;
}

void close (int fd) {
	lock_acquire(&l);
	close_and_remove_file(fd);
	lock_release(&l);
}

/* Helper Functions */
static struct f_desc*
get_fd_struct(struct thread* t, int fd) {
	if(fd < 3) {
		return NULL;
	}
  	return t->fd_table[fd];
}

void close_and_remove_file(int fd) {
	lock_acquire(&file_lock);
	struct list_elem* e;
	struct thread* current = thread_current();
	struct list* open_file_list = &current->open_file_list;

	for(e = list_begin(open_file_list); e != list_end(open_file_list); e = list_next(e)) {
		struct file* f = list_entry(e, struct file, file_elem);

		if(f != NULL && f->fd == fd) {
			list_remove(&f->file_elem);
			current->fd_table[f->fd] = NULL;
			file_close(f);
			break;
		}
	}
	lock_release(&file_lock);
}
