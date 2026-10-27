/* hello: a tiny C program showing the custom libc and Unix headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>

int main(int argc, char **argv)
{
    struct utsname u;
    uname(&u);
    printf("Hello from C on %s %s (%s), pid %d\n", u.sysname, u.release, u.machine, getpid());
    for (int i = 1; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);

    pid_t child = fork();
    if (child == 0) {
        printf("  child %d: fork() works, parent is %d\n", getpid(), getppid());
        exit(7);
    }
    int st;
    waitpid(child, &st, 0);
    printf("  parent: child %d exited with %d\n", child, WEXITSTATUS(st));
    printf("  float formatting: %.3f %g %e\n", 3.14159265, 1e-5, 12345.678);
    return 0;
}
