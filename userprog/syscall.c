#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static struct lock l;
static struct lock file_lock;

static void syscall_handler (struct intr_frame *);
int add_file(struct file* f);
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

	// printf("%04x\t %d\n", sp, syscall_num);
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
			f->eax=remove((char*) (sp+1));
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
	}
}

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
	struct thread* thisThread = thread_current();
	// printf("IN WAIT: %s\n", thisThread->name);
	if (in_child_processes(&(thisThread->children), pid)==NULL){
		return -1;
	}
	// printf("AFTER GET CHILD PROCESSES\n");
	struct thread* dead;
	if (in_all_threads(pid)!=NULL){
		int toReturn = process_wait(pid);
		return toReturn;
	}
	else if (dead = in_grave(pid) !=NULL) {
		return dead->exitCode;
	}
	else{
		return -1;
	}
}

bool create(const char* file, unsigned initial_size) {
	lock_acquire(&l);

	if(file == NULL || !chillPtr(file)) {
		lock_release(&l);
		exit(-1);
	}

	bool success = filesys_create(file, initial_size);

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

	struct file* f = filesys_open(file);

	if(!strcmp(thread_current()->name, file)) {
		file_deny_write(f);
	}

	else if(f == NULL) {
		lock_release(&l);

		return -1;	// ALTERED FROM "RETURN -1" to "EXIT(-1)"
	}

	int fd = add_file(f);
	lock_release(&l);
	return fd;
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

int add_file(struct file* f) {
	lock_acquire(&file_lock);
	struct thread* current = thread_current();
	struct list* open_file_list = &current->open_file_list;

	f->fd = current->fd;
	current->fd++;

	list_push_back(open_file_list, &f->file_elem);
	current->fd_table[f->fd] = f;

	lock_release(&file_lock);
	return f->fd;
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
