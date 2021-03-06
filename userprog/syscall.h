#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"
#include "threads/synch.h"

void syscall_init (void);

/* Additional filesys syscalls */
bool chdir(const char* dir);

bool mkdir(const char* dir);

bool readdir(int fd, char* name);

bool isdir(int fd);

int inumber(int fd);

/* All-purpose syscalls */
void halt();

void exit(int status);

pid_t exec(const char* cmd_line);

int wait(pid_t pid);

bool create(const char* file, unsigned initial_size);

bool remove(const char* file);

int open(const char* file);

int filesize(int fd);

int read(int fd, void* buffer, unsigned size);

int write (int fd, const void *buffer, unsigned size);

void seek (int fd, unsigned position);

unsigned tell (int fd);

void close (int fd);

int chillPtr(void* ptr);

#endif /* userprog/syscall.h */
