#ifndef _UNISTD_H
#define _UNISTD_H
#include <sys/types.h>
#include <kiwi/abi.h>
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int     close(int fd);
off_t   lseek(int fd, off_t off, int whence);
int     dup(int fd);
int     dup2(int oldfd, int newfd);
int     pipe(int fds[2]);
pid_t   fork(void);
int     execve(const char *path, char *const argv[], char *const envp[]);
int     execv(const char *path, char *const argv[]);
pid_t   getpid(void);
pid_t   getppid(void);
int     setpgid(pid_t pid, pid_t pgid);
void   *sbrk(intptr_t incr);
int     chdir(const char *path);
char   *getcwd(char *buf, size_t n);
int     unlink(const char *path);
int     rmdir(const char *path);
int     isatty(int fd);
int     ftruncate(int fd, off_t len);
unsigned sleep(unsigned sec);
int     usleep(unsigned usec);
#endif
