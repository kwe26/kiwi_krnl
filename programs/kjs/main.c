/*
 * kjs - the Kiwi JavaScript host, built on QuickJS.
 *
 *   kjs script.js [args...]   run an ES module (also used via #!/bin/kjs)
 *   kjs -e 'code'             evaluate a global script
 *   kjs --check file.js       compile only (syntax check)
 *   kjs                       interactive REPL (/lib/js/repl.js)
 *
 * The OS is exposed as the native module 'sys' (also the global `sys`);
 * 'kernel' exposes low-level internals and 'gfx' the framebuffer GUI.
 * Bare module names resolve to the system library: import fs from 'fs'
 * loads /lib/js/fs.js.  setTimeout/setInterval and promises are driven by
 * a small event loop that sleeps in the kernel between timers.
 *
 * Builds for Kiwi (default) or for the host with -DKJS_HOST, where Kiwi
 * specific calls have POSIX stand-ins; the host build checks JS syntax at
 * build time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/utsname.h>

#ifdef KJS_HOST
#include <termios.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include "../../include/kiwi/abi.h"
#define KJS_LIBDIR_DEFAULT "jslib"
#else
#include <kiwi/kiwi.h>
#define KJS_LIBDIR_DEFAULT "/lib/js"
#endif

#include "quickjs.h"

static const char *libdir = KJS_LIBDIR_DEFAULT;

/* ------------------------------------------------------------------ */
/* host stand-ins for Kiwi-specific system calls                       */
#ifdef KJS_HOST
extern char **environ;
static int kz_stat(struct kiwi_kzstat *st) { memset(st, 0, sizeof *st); st->page_size = 4096; return 0; }
static int kz_ctl(int op, int arg) { (void)op; (void)arg; errno = ENOSYS; return -1; }
static int kiwi_ps(struct kiwi_procinfo *b, int max) { (void)b; (void)max; return 0; }
static int kiwi_poweroff(void) { exit(0); }
static uint32_t kiwi_uptime_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static int kiwi_sleep_ms(unsigned ms) { return usleep(ms * 1000); }
static pid_t kiwi_spawn(const struct kiwi_spawn *sp)
{
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (sp->fd_in >= 0) posix_spawn_file_actions_adddup2(&fa, sp->fd_in, 0);
    if (sp->fd_out >= 0) posix_spawn_file_actions_adddup2(&fa, sp->fd_out, 1);
    if (sp->fd_err >= 0) posix_spawn_file_actions_adddup2(&fa, sp->fd_err, 2);
    pid_t pid;
    int r = posix_spawn(&pid, sp->path, &fa, NULL, sp->argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (r) {
        errno = r;
        return -1;
    }
    return pid;
}
static struct termios saved_tio;
static int raw_mode;
static int tty_set_raw(int fd, int raw)
{
    if (raw && !raw_mode) {
        struct termios t;
        if (tcgetattr(fd, &saved_tio) < 0)
            return -1;
        t = saved_tio;
        t.c_lflag &= ~(ICANON | ECHO | ISIG);
        t.c_iflag &= ~(ICRNL | IXON);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &t);
    } else if (!raw && raw_mode) {
        tcsetattr(fd, TCSANOW, &saved_tio);
    }
    raw_mode = raw;
    return 0;
}
static int tty_set_fg(int fd, pid_t pg) { (void)fd; (void)pg; return 0; }
static int tty_get_size(int fd, int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) < 0 || !ws.ws_row) {
        *rows = 25;
        *cols = 80;
        return 0;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* helpers                                                            */

static JSValue throw_errno(JSContext *ctx, const char *what, const char *arg)
{
    int e = errno;
    char msg[384];
    if (arg)
        snprintf(msg, sizeof msg, "%s %s: %s", what, arg, strerror(e));
    else
        snprintf(msg, sizeof msg, "%s: %s", what, strerror(e));
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg));
    JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, e));
    JS_SetPropertyStr(ctx, err, "code", JS_NewString(ctx, strerror(e)));
    return JS_Throw(ctx, err);
}

static int arg_int(JSContext *ctx, int argc, JSValueConst *argv, int i, int def)
{
    int32_t v = def;
    if (i < argc && !JS_IsUndefined(argv[i]))
        JS_ToInt32(ctx, &v, argv[i]);
    return v;
}

static int arg_bool(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
    return i < argc && JS_ToBool(ctx, argv[i]) > 0;
}

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* growable buffer for JS_PrintValue */
struct dbuf_s {
    char *buf;
    size_t len, cap;
};

static void dbuf_write(void *opaque, const char *s, size_t n)
{
    struct dbuf_s *d = opaque;
    if (d->len + n + 1 > d->cap) {
        size_t c = (d->cap ? d->cap * 2 : 256);
        while (c < d->len + n + 1)
            c *= 2;
        char *nb = realloc(d->buf, c);
        if (!nb)
            return;
        d->buf = nb;
        d->cap = c;
    }
    memcpy(d->buf + d->len, s, n);
    d->len += n;
    d->buf[d->len] = 0;
}

static void inspect(JSContext *ctx, JSValueConst v, struct dbuf_s *d)
{
    JSPrintValueOptions opt;
    JS_PrintValueSetDefaultOptions(&opt);
    opt.max_depth = 4;
    opt.max_item_count = 100;
    JS_PrintValue(ctx, dbuf_write, d, v, &opt);
}

static void print_error(JSContext *ctx, JSValueConst exc)
{
    struct dbuf_s d = { 0 };
    inspect(ctx, exc, &d);
    if (JS_IsError(ctx, exc)) {
        /* JS_PrintValue includes the stack for Error objects */
    }
    dbuf_write(&d, "\n", 1);
    write_all(2, "\033[31m", 5);
    write_all(2, d.buf, d.len);
    write_all(2, "\033[0m", 4);
    free(d.buf);
}

static void dump_exception(JSContext *ctx)
{
    JSValue e = JS_GetException(ctx);
    print_error(ctx, e);
    JS_FreeValue(ctx, e);
}

static char *load_file(const char *path, size_t *len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap + 1);
    for (;;) {
        if (n == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                close(fd);
                errno = ENOMEM;
                return 0;
            }
            buf = nb;
        }
        ssize_t r = read(fd, buf + n, cap - n);
        if (r < 0) {
            free(buf);
            close(fd);
            return 0;
        }
        if (r == 0)
            break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = 0;
    *len = n;
    return buf;
}

static char **to_argv(JSContext *ctx, JSValueConst arr, int *pn)
{
    JSValue lv = JS_GetPropertyStr(ctx, arr, "length");
    int32_t n = 0;
    JS_ToInt32(ctx, &n, lv);
    JS_FreeValue(ctx, lv);
    if (n < 0 || n > 64)
        n = 0;
    char **av = calloc((size_t)n + 1, sizeof *av);
    for (int i = 0; i < n; i++) {
        JSValue v = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
        const char *s = JS_ToCString(ctx, v);
        av[i] = strdup(s ? s : "");
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, v);
    }
    *pn = n;
    return av;
}

static void free_argv(char **av, int n)
{
    for (int i = 0; i < n; i++)
        free(av[i]);
    free(av);
}

/* ------------------------------------------------------------------ */
/* sys.* : files                                                      */

static JSValue js_write(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int fd = arg_int(ctx, argc, argv, 0, 1);
    size_t len;
    uint8_t *ab = argc > 1 ? JS_GetArrayBuffer(ctx, &len, argv[1]) : 0;
    if (ab) {
        if (write_all(fd, (const char *)ab, len) < 0)
            return throw_errno(ctx, "write", 0);
        return JS_NewInt32(ctx, (int)len);
    }
    JS_FreeValue(ctx, JS_GetException(ctx));     /* not an ArrayBuffer */
    const char *s = JS_ToCStringLen(ctx, &len, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (!s)
        return JS_EXCEPTION;
    int r = write_all(fd, s, len);
    JS_FreeCString(ctx, s);
    if (r < 0)
        return throw_errno(ctx, "write", 0);
    return JS_NewInt32(ctx, (int)len);
}

static JSValue js_read(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int fd = arg_int(ctx, argc, argv, 0, 0);
    int n = arg_int(ctx, argc, argv, 1, 4096);
    int binary = arg_bool(ctx, argc, argv, 2);
    if (n <= 0)
        n = 4096;
    char *buf = malloc((size_t)n);
    if (!buf)
        return JS_ThrowOutOfMemory(ctx);
    ssize_t r = read(fd, buf, (size_t)n);
    if (r < 0) {
        free(buf);
        return throw_errno(ctx, "read", 0);
    }
    JSValue v = binary ? JS_NewArrayBufferCopy(ctx, (uint8_t *)buf, (size_t)r)
                       : JS_NewStringLen(ctx, buf, (size_t)r);
    free(buf);
    return v;
}

static JSValue js_open(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    int flags = arg_int(ctx, argc, argv, 1, O_RDONLY);
    int fd = open(path, flags, 0644);
    JSValue r = fd < 0 ? throw_errno(ctx, "open", path) : JS_NewInt32(ctx, fd);
    JS_FreeCString(ctx, path);
    return r;
}

static JSValue js_close(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, close(arg_int(ctx, argc, argv, 0, -1)));
}

static JSValue js_seek(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    off_t r = lseek(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, 0),
                    arg_int(ctx, argc, argv, 2, SEEK_SET));
    if (r < 0)
        return throw_errno(ctx, "seek", 0);
    return JS_NewInt64(ctx, r);
}

static JSValue js_readFile(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    size_t len;
    char *buf = load_file(path, &len);
    JSValue r;
    if (!buf)
        r = throw_errno(ctx, "open", path);
    else if (arg_bool(ctx, argc, argv, 1))
        r = JS_NewArrayBufferCopy(ctx, (uint8_t *)buf, len);
    else
        r = JS_NewStringLen(ctx, buf, len);
    free(buf);
    JS_FreeCString(ctx, path);
    return r;
}

static JSValue js_writeFile(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    int append = arg_bool(ctx, argc, argv, 2);
    int fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) {
        JSValue e = throw_errno(ctx, "open", path);
        JS_FreeCString(ctx, path);
        return e;
    }
    size_t len;
    const char *data;
    uint8_t *ab = JS_GetArrayBuffer(ctx, &len, argv[1]);
    if (ab) {
        data = (const char *)ab;
    } else {
        JS_FreeValue(ctx, JS_GetException(ctx));
        data = JS_ToCStringLen(ctx, &len, argv[1]);
    }
    int r = data ? write_all(fd, data, len) : -1;
    if (!ab && data)
        JS_FreeCString(ctx, data);
    close(fd);
    JSValue res = r < 0 ? throw_errno(ctx, "write", path) : JS_NewInt32(ctx, (int)len);
    JS_FreeCString(ctx, path);
    return res;
}

static JSValue stat_obj(JSContext *ctx, struct stat *st)
{
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "size", JS_NewInt64(ctx, st->st_size));
#ifdef KJS_HOST
    JS_SetPropertyStr(ctx, o, "csize", JS_NewInt64(ctx, st->st_size));
#else
    JS_SetPropertyStr(ctx, o, "csize", JS_NewInt64(ctx, st->st_csize));
#endif
    JS_SetPropertyStr(ctx, o, "mode", JS_NewInt32(ctx, (int)st->st_mode));
    JS_SetPropertyStr(ctx, o, "ino", JS_NewInt64(ctx, (int64_t)st->st_ino));
    JS_SetPropertyStr(ctx, o, "mtime", JS_NewInt64(ctx, (int64_t)st->st_mtime));
    JS_SetPropertyStr(ctx, o, "isDir", JS_NewBool(ctx, S_ISDIR(st->st_mode)));
    JS_SetPropertyStr(ctx, o, "isFile", JS_NewBool(ctx, S_ISREG(st->st_mode)));
    JS_SetPropertyStr(ctx, o, "isTTY", JS_NewBool(ctx, S_ISCHR(st->st_mode)));
    return o;
}

static JSValue js_stat(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    struct stat st;
    int r = stat(path, &st);
    JS_FreeCString(ctx, path);
    return r < 0 ? JS_NULL : stat_obj(ctx, &st);
}

static JSValue js_fstat(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct stat st;
    if (fstat(arg_int(ctx, argc, argv, 0, -1), &st) < 0)
        return throw_errno(ctx, "fstat", 0);
    return stat_obj(ctx, &st);
}

static JSValue js_readdir(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!path)
        return JS_EXCEPTION;
    DIR *d = opendir(path);
    if (!d) {
        JSValue e = throw_errno(ctx, "readdir", path);
        JS_FreeCString(ctx, path);
        return e;
    }
    JSValue arr = JS_NewArray(ctx);
    uint32_t i = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' && (!de->d_name[1] || (de->d_name[1] == '.' && !de->d_name[2])))
            continue;
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, de->d_name));
#ifdef KJS_HOST
        char full[1024];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", path, de->d_name);
        int ok = stat(full, &st) == 0;
        JS_SetPropertyStr(ctx, o, "type", JS_NewString(ctx, ok && S_ISDIR(st.st_mode) ? "dir" : "file"));
        JS_SetPropertyStr(ctx, o, "size", JS_NewInt64(ctx, ok ? st.st_size : 0));
        JS_SetPropertyStr(ctx, o, "csize", JS_NewInt64(ctx, ok ? st.st_size : 0));
#else
        JS_SetPropertyStr(ctx, o, "type", JS_NewString(ctx, de->d_type == DT_DIR ? "dir" : "file"));
        JS_SetPropertyStr(ctx, o, "size", JS_NewInt64(ctx, de->d_size));
        JS_SetPropertyStr(ctx, o, "csize", JS_NewInt64(ctx, de->d_csize));
#endif
        JS_SetPropertyUint32(ctx, arr, i++, o);
    }
    closedir(d);
    JS_FreeCString(ctx, path);
    return arr;
}

#define PATH_OP(name, call, label)                                                  \
static JSValue name(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) \
{                                                                                   \
    const char *path = JS_ToCString(ctx, argv[0]);                                  \
    if (!path)                                                                      \
        return JS_EXCEPTION;                                                        \
    int r = call;                                                                   \
    JSValue v = r < 0 ? throw_errno(ctx, label, path) : JS_UNDEFINED;               \
    JS_FreeCString(ctx, path);                                                      \
    return v;                                                                       \
}
PATH_OP(js_mkdir, mkdir(path, 0755), "mkdir")
PATH_OP(js_unlink, unlink(path), "unlink")
PATH_OP(js_rmdir, rmdir(path), "rmdir")
PATH_OP(js_chdir, chdir(path), "cd")

static JSValue js_rename(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *a = JS_ToCString(ctx, argv[0]);
    const char *b = JS_ToCString(ctx, argv[1]);
    JSValue v = JS_UNDEFINED;
    if (!a || !b)
        v = JS_EXCEPTION;
    else if (rename(a, b) < 0)
        v = throw_errno(ctx, "rename", a);
    JS_FreeCString(ctx, a);
    JS_FreeCString(ctx, b);
    return v;
}

static JSValue js_getcwd(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    char buf[512];
    if (!getcwd(buf, sizeof buf))
        return throw_errno(ctx, "getcwd", 0);
    return JS_NewString(ctx, buf);
}

static JSValue js_pipe(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int fds[2];
    if (pipe(fds) < 0)
        return throw_errno(ctx, "pipe", 0);
    JSValue a = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, fds[0]));
    JS_SetPropertyUint32(ctx, a, 1, JS_NewInt32(ctx, fds[1]));
    return a;
}

static JSValue js_dup(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int r = dup(arg_int(ctx, argc, argv, 0, -1));
    return r < 0 ? throw_errno(ctx, "dup", 0) : JS_NewInt32(ctx, r);
}

static JSValue js_dup2(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int r = dup2(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, -1));
    return r < 0 ? throw_errno(ctx, "dup2", 0) : JS_NewInt32(ctx, r);
}

/* ------------------------------------------------------------------ */
/* sys.* : processes                                                  */

static JSValue js_spawn(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    int n = 0;
    char **av;
    if (argc > 1 && JS_IsArray(ctx, argv[1])) {
        av = to_argv(ctx, argv[1], &n);
    } else {
        av = calloc(2, sizeof *av);
        av[0] = strdup(path);
        n = 1;
    }
    struct kiwi_spawn sp = { path, av, -1, -1, -1, 0 };
    if (argc > 2 && JS_IsObject(argv[2])) {
        JSValue o = argv[2], v;
        int32_t x;
        v = JS_GetPropertyStr(ctx, o, "stdin");
        if (!JS_IsUndefined(v) && JS_ToInt32(ctx, &x, v) == 0) sp.fd_in = x;
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, o, "stdout");
        if (!JS_IsUndefined(v) && JS_ToInt32(ctx, &x, v) == 0) sp.fd_out = x;
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, o, "stderr");
        if (!JS_IsUndefined(v) && JS_ToInt32(ctx, &x, v) == 0) sp.fd_err = x;
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, o, "pgid");
        if (!JS_IsUndefined(v) && JS_ToInt32(ctx, &x, v) == 0) sp.pgid = x;
        JS_FreeValue(ctx, v);
    }
    pid_t pid = kiwi_spawn(&sp);
    JSValue r = pid < 0 ? throw_errno(ctx, "spawn", path) : JS_NewInt32(ctx, pid);
    free_argv(av, n);
    JS_FreeCString(ctx, path);
    return r;
}

static JSValue js_exec(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    int n = 0;
    char **av = to_argv(ctx, argv[1], &n);
    execv(path, av);
    JSValue e = throw_errno(ctx, "exec", path);
    free_argv(av, n);
    JS_FreeCString(ctx, path);
    return e;
}

static JSValue js_wait(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int pid = arg_int(ctx, argc, argv, 0, -1);
    int st = 0;
    pid_t r = waitpid(pid, &st, arg_bool(ctx, argc, argv, 1) ? WNOHANG : 0);
    if (r < 0 && errno == ECHILD)
        return JS_NULL;
    if (r < 0)
        return throw_errno(ctx, "wait", 0);
    if (r == 0)
        return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "pid", JS_NewInt32(ctx, r));
    JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, st));
    JS_SetPropertyStr(ctx, o, "code", JS_NewInt32(ctx, WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st)));
    JS_SetPropertyStr(ctx, o, "signal", JS_NewInt32(ctx, WIFSIGNALED(st) ? WTERMSIG(st) : 0));
    return o;
}

static JSValue js_exit(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    fflush(stdout);
    exit(arg_int(ctx, argc, argv, 0, 0));
}

static JSValue js_kill(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    if (kill(arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, SIGTERM)) < 0)
        return throw_errno(ctx, "kill", 0);
    return JS_UNDEFINED;
}

static JSValue js_getpid(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, getpid());
}

static JSValue js_getppid(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, getppid());
}

static JSValue js_setpgid(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, setpgid(arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0)));
}

static JSValue js_sleep(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    kiwi_sleep_ms((unsigned)arg_int(ctx, argc, argv, 0, 0));
    return JS_UNDEFINED;
}

static JSValue js_uptime(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewInt64(ctx, kiwi_uptime_ms());
}

static JSValue js_time(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewInt64(ctx, (int64_t)time(0));
}

static JSValue js_ps(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_procinfo pi[64];
    int n = kiwi_ps(pi, 64);
    JSValue arr = JS_NewArray(ctx);
    static const char *states[] = { "?", "R", "S", "Z" };
    for (int i = 0; i < n; i++) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "pid", JS_NewInt32(ctx, pi[i].pid));
        JS_SetPropertyStr(ctx, o, "ppid", JS_NewInt32(ctx, pi[i].ppid));
        JS_SetPropertyStr(ctx, o, "pgid", JS_NewInt32(ctx, pi[i].pgid));
        JS_SetPropertyStr(ctx, o, "state", JS_NewString(ctx, states[pi[i].state & 3]));
        JS_SetPropertyStr(ctx, o, "resident", JS_NewInt64(ctx, pi[i].resident));
        JS_SetPropertyStr(ctx, o, "compressed", JS_NewInt64(ctx, pi[i].compressed));
        JS_SetPropertyStr(ctx, o, "ticks", JS_NewInt64(ctx, pi[i].ticks));
        JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, pi[i].name));
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
    }
    return arr;
}

static JSValue js_uname(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct utsname u;
    if (uname(&u) < 0)
        return throw_errno(ctx, "uname", 0);
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "sysname", JS_NewString(ctx, u.sysname));
    JS_SetPropertyStr(ctx, o, "nodename", JS_NewString(ctx, u.nodename));
    JS_SetPropertyStr(ctx, o, "release", JS_NewString(ctx, u.release));
    JS_SetPropertyStr(ctx, o, "version", JS_NewString(ctx, u.version));
    JS_SetPropertyStr(ctx, o, "machine", JS_NewString(ctx, u.machine));
    return o;
}

static JSValue js_poweroff(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    kiwi_poweroff();
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------ */
/* sys.* : terminal and memory compression                            */

static JSValue js_ttyRaw(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    tty_set_raw(0, arg_bool(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

static JSValue js_ttySetFg(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    tty_set_fg(0, arg_int(ctx, argc, argv, 0, 0));
    return JS_UNDEFINED;
}

static JSValue js_ttySize(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int rows = 25, cols = 80;
    tty_get_size(1, &rows, &cols);
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "rows", JS_NewInt32(ctx, rows));
    JS_SetPropertyStr(ctx, o, "cols", JS_NewInt32(ctx, cols));
    return o;
}

static JSValue js_isatty(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewBool(ctx, isatty(arg_int(ctx, argc, argv, 0, 0)));
}

static JSValue js_kzstat(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_kzstat k;
    if (kz_stat(&k) < 0)
        return throw_errno(ctx, "kzstat", 0);
    JSValue o = JS_NewObject(ctx);
#define F(x) JS_SetPropertyStr(ctx, o, #x, JS_NewInt64(ctx, k.x))
    F(page_size); F(total_frames); F(free_frames); F(user_resident); F(budget);
    F(proactive); F(z_pages); F(z_slots); F(z_same); F(z_lz); F(z_lzh); F(z_raw); F(z_bytes);
    F(z_dedup); F(z_pageouts); F(z_pageins); F(fs_files); F(fs_bytes); F(fs_zbytes);
    F(heap_total); F(heap_used);
#undef F
    return o;
}

static JSValue js_kzctl(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int r = kz_ctl(arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0));
    if (r < 0)
        return throw_errno(ctx, "kzctl", 0);
    return JS_NewInt32(ctx, r);
}

/* ------------------------------------------------------------------ */
/* sys.* : the engine itself                                          */

static JSValue js_gc(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static JSValue js_memoryUsage(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    JSMemoryUsage u;
    JS_ComputeMemoryUsage(JS_GetRuntime(ctx), &u);
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "malloc", JS_NewInt64(ctx, u.malloc_size));
    JS_SetPropertyStr(ctx, o, "used", JS_NewInt64(ctx, u.memory_used_size));
    JS_SetPropertyStr(ctx, o, "objects", JS_NewInt64(ctx, u.obj_count));
    JS_SetPropertyStr(ctx, o, "strings", JS_NewInt64(ctx, u.str_count));
    JS_SetPropertyStr(ctx, o, "functions", JS_NewInt64(ctx, u.js_func_count));
    return o;
}

static JSValue js_evalScript(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    size_t len;
    const char *code = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!code)
        return JS_EXCEPTION;
    const char *name = argc > 1 ? JS_ToCString(ctx, argv[1]) : 0;
    JSValue r = JS_Eval(ctx, code, len, name ? name : "<eval>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(ctx, code);
    if (name)
        JS_FreeCString(ctx, name);
    return r;
}

static JSValue js_inspect(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct dbuf_s d = { 0 };
    inspect(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &d);
    JSValue r = JS_NewStringLen(ctx, d.buf ? d.buf : "", d.len);
    free(d.buf);
    return r;
}

static const JSCFunctionListEntry sys_funcs[] = {
    JS_CFUNC_DEF("write", 2, js_write),
    JS_CFUNC_DEF("read", 3, js_read),
    JS_CFUNC_DEF("open", 2, js_open),
    JS_CFUNC_DEF("close", 1, js_close),
    JS_CFUNC_DEF("seek", 3, js_seek),
    JS_CFUNC_DEF("readFile", 2, js_readFile),
    JS_CFUNC_DEF("writeFile", 3, js_writeFile),
    JS_CFUNC_DEF("stat", 1, js_stat),
    JS_CFUNC_DEF("fstat", 1, js_fstat),
    JS_CFUNC_DEF("readdir", 1, js_readdir),
    JS_CFUNC_DEF("mkdir", 1, js_mkdir),
    JS_CFUNC_DEF("unlink", 1, js_unlink),
    JS_CFUNC_DEF("rmdir", 1, js_rmdir),
    JS_CFUNC_DEF("rename", 2, js_rename),
    JS_CFUNC_DEF("chdir", 1, js_chdir),
    JS_CFUNC_DEF("getcwd", 0, js_getcwd),
    JS_CFUNC_DEF("pipe", 0, js_pipe),
    JS_CFUNC_DEF("dup", 1, js_dup),
    JS_CFUNC_DEF("dup2", 2, js_dup2),
    JS_CFUNC_DEF("spawn", 3, js_spawn),
    JS_CFUNC_DEF("exec", 2, js_exec),
    JS_CFUNC_DEF("wait", 2, js_wait),
    JS_CFUNC_DEF("exit", 1, js_exit),
    JS_CFUNC_DEF("kill", 2, js_kill),
    JS_CFUNC_DEF("getpid", 0, js_getpid),
    JS_CFUNC_DEF("getppid", 0, js_getppid),
    JS_CFUNC_DEF("setpgid", 2, js_setpgid),
    JS_CFUNC_DEF("sleep", 1, js_sleep),
    JS_CFUNC_DEF("uptime", 0, js_uptime),
    JS_CFUNC_DEF("time", 0, js_time),
    JS_CFUNC_DEF("ps", 0, js_ps),
    JS_CFUNC_DEF("uname", 0, js_uname),
    JS_CFUNC_DEF("poweroff", 0, js_poweroff),
    JS_CFUNC_DEF("ttyRaw", 1, js_ttyRaw),
    JS_CFUNC_DEF("ttySetFg", 1, js_ttySetFg),
    JS_CFUNC_DEF("ttySize", 0, js_ttySize),
    JS_CFUNC_DEF("isatty", 1, js_isatty),
    JS_CFUNC_DEF("kzstat", 0, js_kzstat),
    JS_CFUNC_DEF("kzctl", 2, js_kzctl),
    JS_CFUNC_DEF("gc", 0, js_gc),
    JS_CFUNC_DEF("memoryUsage", 0, js_memoryUsage),
    JS_CFUNC_DEF("evalScript", 2, js_evalScript),
    JS_CFUNC_DEF("inspect", 1, js_inspect),
    JS_PROP_INT32_DEF("O_RDONLY", O_RDONLY, 0),
    JS_PROP_INT32_DEF("O_WRONLY", O_WRONLY, 0),
    JS_PROP_INT32_DEF("O_RDWR", O_RDWR, 0),
    JS_PROP_INT32_DEF("O_CREAT", O_CREAT, 0),
    JS_PROP_INT32_DEF("O_TRUNC", O_TRUNC, 0),
    JS_PROP_INT32_DEF("O_APPEND", O_APPEND, 0),
    JS_PROP_INT32_DEF("O_EXCL", O_EXCL, 0),
    JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, 0),
    JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, 0),
    JS_PROP_INT32_DEF("SEEK_END", SEEK_END, 0),
    JS_PROP_INT32_DEF("SIGINT", SIGINT, 0),
    JS_PROP_INT32_DEF("SIGKILL", SIGKILL, 0),
    JS_PROP_INT32_DEF("SIGTERM", SIGTERM, 0),
    JS_PROP_INT32_DEF("KZ_SET_BUDGET", KZ_SET_BUDGET, 0),
    JS_PROP_INT32_DEF("KZ_GET_BUDGET", KZ_GET_BUDGET, 0),
    JS_PROP_INT32_DEF("KZ_COMPRESS_ALL", KZ_COMPRESS_ALL, 0),
    JS_PROP_INT32_DEF("KZ_SET_PROACTIVE", KZ_SET_PROACTIVE, 0),
#ifdef KJS_HOST
    JS_PROP_INT32_DEF("isKiwi", 0, 0),
#else
    JS_PROP_INT32_DEF("isKiwi", 1, 0),
#endif
};

/* ------------------------------------------------------------------ */
/* console, timers                                                    */

static JSValue js_console_out(JSContext *ctx, int fd, int argc, JSValueConst *argv)
{
    struct dbuf_s d = { 0 };
    for (int i = 0; i < argc; i++) {
        if (i)
            dbuf_write(&d, " ", 1);
        if (JS_IsString(argv[i])) {
            size_t len;
            const char *s = JS_ToCStringLen(ctx, &len, argv[i]);
            if (s) {
                dbuf_write(&d, s, len);
                JS_FreeCString(ctx, s);
            }
        } else {
            inspect(ctx, argv[i], &d);
        }
    }
    dbuf_write(&d, "\n", 1);
    write_all(fd, d.buf, d.len);
    free(d.buf);
    return JS_UNDEFINED;
}

static JSValue js_log(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return js_console_out(ctx, 1, argc, argv);
}

static JSValue js_error(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return js_console_out(ctx, 2, argc, argv);
}

#define MAX_TIMERS 64
static struct timer {
    int id, used;
    uint32_t deadline, interval;
    JSValue fn;
} timers[MAX_TIMERS];
static int next_timer_id = 1;

static JSValue add_timer(JSContext *ctx, int argc, JSValueConst *argv, int repeat)
{
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "timer callback must be a function");
    int ms = arg_int(ctx, argc, argv, 1, 0);
    if (ms < 0)
        ms = 0;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].used) {
            timers[i] = (struct timer){ next_timer_id++, 1, kiwi_uptime_ms() + (uint32_t)ms,
                                        repeat ? (uint32_t)(ms ? ms : 1) : 0,
                                        JS_DupValue(ctx, argv[0]) };
            return JS_NewInt32(ctx, timers[i].id);
        }
    }
    return JS_ThrowRangeError(ctx, "too many timers");
}

static JSValue js_setTimeout(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return add_timer(ctx, argc, argv, 0);
}

static JSValue js_setInterval(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return add_timer(ctx, argc, argv, 1);
}

static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int id = arg_int(ctx, argc, argv, 0, 0);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].used && timers[i].id == id) {
            JS_FreeValue(ctx, timers[i].fn);
            timers[i].used = 0;
        }
    }
    return JS_UNDEFINED;
}

/* run jobs and timers until `promise` settles (or forever if it is
 * JS_UNDEFINED and work remains).  Returns 0, or -1 if an uncaught
 * exception was reported. */
static int run_loop(JSContext *ctx, JSValueConst promise)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    int failed = 0;
    for (;;) {
        JSContext *c;
        int err;
        while ((err = JS_ExecutePendingJob(rt, &c)) != 0) {
            if (err < 0) {
                dump_exception(c);
                failed = 1;
            }
        }
        if (!JS_IsUndefined(promise) && JS_PromiseState(ctx, (JSValue)promise) != JS_PROMISE_PENDING)
            return failed ? -1 : 0;
        int best = -1;
        for (int i = 0; i < MAX_TIMERS; i++)
            if (timers[i].used && (best < 0 || (int32_t)(timers[i].deadline - timers[best].deadline) < 0))
                best = i;
        if (best < 0)
            return failed ? -1 : 0;
        int32_t wait = (int32_t)(timers[best].deadline - kiwi_uptime_ms());
        if (wait > 0)
            kiwi_sleep_ms((unsigned)wait);
        JSValue fn = JS_DupValue(ctx, timers[best].fn);
        if (timers[best].interval) {
            timers[best].deadline += timers[best].interval;
        } else {
            JS_FreeValue(ctx, timers[best].fn);
            timers[best].used = 0;
        }
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 0, 0);
        JS_FreeValue(ctx, fn);
        if (JS_IsException(r)) {
            dump_exception(ctx);
            failed = 1;
        }
        JS_FreeValue(ctx, r);
    }
}

/* ------------------------------------------------------------------ */
/* modules                                                            */

static JSValue script_args;      /* the argv array, shared by global and module */

static int sys_module_init(JSContext *ctx, JSModuleDef *m)
{
    JS_SetModuleExport(ctx, m, "argv", JS_DupValue(ctx, script_args));
    JS_SetModuleExport(ctx, m, "libdir", JS_NewString(ctx, libdir));
    return JS_SetModuleExportList(ctx, m, sys_funcs, sizeof sys_funcs / sizeof sys_funcs[0]);
}

JSModuleDef *kjs_module_kernel(JSContext *ctx, const char *name);
JSModuleDef *kjs_module_gfx(JSContext *ctx, const char *name);

static int native_module(const char *name)
{
    return !strcmp(name, "sys") || !strcmp(name, "kernel") || !strcmp(name, "gfx");
}

static char *normalize(JSContext *ctx, const char *base, const char *name, void *opaque)
{
    char buf[512];
    if (native_module(name)) {
        snprintf(buf, sizeof buf, "%s", name);
    } else if (name[0] == '/') {
        snprintf(buf, sizeof buf, "%s", name);
    } else if (name[0] == '.') {
        /* relative to the importing module's directory */
        const char *slash = strrchr(base, '/');
        int dl = slash ? (int)(slash - base) : 0;
        snprintf(buf, sizeof buf, "%.*s%s%s", dl, base, dl ? "/" : "", name);
        /* fold "./" and "dir/../" */
        char out[512];
        int o = 0;
        const char *p = buf;
        if (*p == '/')
            out[o++] = *p++;
        while (*p) {
            const char *s = p;
            while (*p && *p != '/')
                p++;
            int l = (int)(p - s);
            if (l == 1 && s[0] == '.') {
            } else if (l == 2 && s[0] == '.' && s[1] == '.' && o > 0 &&
                       !(o >= 2 && out[o - 1] == '.' && out[o - 2] == '.')) {
                while (o > 0 && out[o - 1] != '/')
                    o--;
                if (o > 0 && o != 1)
                    o--;
            } else {
                if (o > 0 && out[o - 1] != '/')
                    out[o++] = '/';
                memcpy(out + o, s, (size_t)l);
                o += l;
            }
            if (*p == '/')
                p++;
        }
        out[o] = 0;
        snprintf(buf, sizeof buf, "%s", out);
    } else {
        /* bare name: system library */
        size_t l = strlen(name);
        int has_ext = l > 3 && strcmp(name + l - 3, ".js") == 0;
        snprintf(buf, sizeof buf, "%s/%s%s", libdir, name, has_ext ? "" : ".js");
    }
    size_t n = strlen(buf) + 1;
    char *r = js_malloc(ctx, n);
    if (r)
        memcpy(r, buf, n);
    return r;
}

static void set_import_meta(JSContext *ctx, JSValueConst fn, const char *name, int is_main)
{
    JSModuleDef *m = JS_VALUE_GET_PTR(fn);
    JSValue meta = JS_GetImportMeta(ctx, m);
    JS_SetPropertyStr(ctx, meta, "url", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, meta, "main", JS_NewBool(ctx, is_main));
    JS_FreeValue(ctx, meta);
}

static JSModuleDef *loader(JSContext *ctx, const char *name, void *opaque)
{
    if (strcmp(name, "kernel") == 0)
        return kjs_module_kernel(ctx, name);
    if (strcmp(name, "gfx") == 0)
        return kjs_module_gfx(ctx, name);
    if (strcmp(name, "sys") == 0) {
        JSModuleDef *m = JS_NewCModule(ctx, "sys", sys_module_init);
        if (m) {
            JS_AddModuleExport(ctx, m, "argv");
            JS_AddModuleExport(ctx, m, "libdir");
            JS_AddModuleExportList(ctx, m, sys_funcs, sizeof sys_funcs / sizeof sys_funcs[0]);
        }
        return m;
    }
    size_t len;
    char *buf = load_file(name, &len);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "cannot load module '%s'", name);
        return 0;
    }
    JSValue fn = JS_Eval(ctx, buf, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(buf);
    if (JS_IsException(fn))
        return 0;
    set_import_meta(ctx, fn, name, 0);
    JSModuleDef *m = JS_VALUE_GET_PTR(fn);
    JS_FreeValue(ctx, fn);
    return m;
}

/* ------------------------------------------------------------------ */

static JSContext *new_context(JSRuntime *rt, int argc, char **argv)
{
    JSContext *ctx = JS_NewContext(rt);
    JSValue g = JS_GetGlobalObject(ctx);

    JSValue sys = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, sys, sys_funcs, sizeof sys_funcs / sizeof sys_funcs[0]);
    JSValue args = JS_NewArray(ctx);
    for (int i = 0; i < argc; i++)
        JS_SetPropertyUint32(ctx, args, (uint32_t)i, JS_NewString(ctx, argv[i]));
    script_args = JS_DupValue(ctx, args);
    JS_SetPropertyStr(ctx, g, "scriptArgs", JS_DupValue(ctx, args));
    JS_SetPropertyStr(ctx, sys, "argv", args);
    JS_SetPropertyStr(ctx, sys, "libdir", JS_NewString(ctx, libdir));
    JS_SetPropertyStr(ctx, g, "sys", sys);

    JSValue con = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, con, "log", JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, con, "info", JS_NewCFunction(ctx, js_log, "info", 1));
    JS_SetPropertyStr(ctx, con, "error", JS_NewCFunction(ctx, js_error, "error", 1));
    JS_SetPropertyStr(ctx, con, "warn", JS_NewCFunction(ctx, js_error, "warn", 1));
    JS_SetPropertyStr(ctx, g, "console", con);
    JS_SetPropertyStr(ctx, g, "print", JS_NewCFunction(ctx, js_log, "print", 1));
    JS_SetPropertyStr(ctx, g, "setTimeout", JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, g, "setInterval", JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx, g, "clearTimeout", JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, g, "clearInterval", JS_NewCFunction(ctx, js_clearTimeout, "clearInterval", 1));
    JS_FreeValue(ctx, g);
    return ctx;
}

static int run_module_file(JSContext *ctx, const char *path, int check_only)
{
    size_t len;
    char *buf = load_file(path, &len);
    if (!buf) {
        fprintf(stderr, "kjs: %s: %s\n", path, strerror(errno));
        return 127;
    }
    JSValue fn = JS_Eval(ctx, buf, len, path, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(buf);
    if (JS_IsException(fn)) {
        dump_exception(ctx);
        return 1;
    }
    if (check_only) {
        JS_FreeValue(ctx, fn);
        return 0;
    }
    set_import_meta(ctx, fn, path, 1);
    JSValue p = JS_EvalFunction(ctx, fn);
    if (JS_IsException(p)) {
        dump_exception(ctx);
        return 1;
    }
    int rc = run_loop(ctx, p);
    if (JS_PromiseState(ctx, p) == JS_PROMISE_REJECTED) {
        JSValue e = JS_PromiseResult(ctx, p);
        print_error(ctx, e);
        JS_FreeValue(ctx, e);
        rc = -1;
    }
    JS_FreeValue(ctx, p);
    if (rc == 0)
        rc = run_loop(ctx, JS_UNDEFINED);    /* remaining timers */
    return rc < 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
#ifdef KJS_HOST
    if (getenv("KJS_LIB"))
        libdir = getenv("KJS_LIB");
#endif
    JSRuntime *rt = JS_NewRuntime();
    JS_SetMaxStackSize(rt, 1024 * 1024);
    JS_SetModuleLoaderFunc(rt, normalize, loader, 0);

    int i = 1, check = 0;
    const char *expr = 0;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--check") == 0 || strcmp(argv[i], "-c") == 0) {
            check = 1;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            expr = argv[++i];
        } else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            fprintf(stderr, "usage: kjs [--check] [-e code] [script.js [args...]]\n");
            return 2;
        }
        i++;
    }

    JSContext *ctx = new_context(rt, argc - i, argv + i);
    int rc = 0;
    if (expr) {
        JSValue v = JS_Eval(ctx, expr, strlen(expr), "<cmdline>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(v)) {
            dump_exception(ctx);
            rc = 1;
        } else if (!JS_IsUndefined(v)) {
            js_log(ctx, JS_UNDEFINED, 1, (JSValueConst *)&v);
        }
        JS_FreeValue(ctx, v);
        if (rc == 0 && run_loop(ctx, JS_UNDEFINED) < 0)
            rc = 1;
    } else if (i < argc) {
        rc = run_module_file(ctx, argv[i], check);
    } else {
        char repl[256];
        snprintf(repl, sizeof repl, "%s/repl.js", libdir);
        rc = run_module_file(ctx, repl, 0);
    }
    fflush(stdout);
    /* Skipping JS_FreeContext/JS_FreeRuntime on purpose: the process is
     * about to exit and the kernel reclaims everything at once. */
    return rc;
}
