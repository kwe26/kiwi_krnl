/*
 * ramfs: an in-memory filesystem whose files are stored compressed.
 *
 * At rest a file is only its packed blob: KLZ+Huffman, KLZ, or raw bytes,
 * whichever is smallest.
 * Opening a file inflates it into a cache buffer shared by all open
 * descriptors; the last close re-compresses it (heavy level) if it was
 * modified and drops the cache.  Executables are inflated only for the
 * duration of exec().
 */
#include "kernel.h"
#include "lz.h"

enum { T_FILE = 1, T_DIR = 2, T_DEV = 3 };

struct inode {
    uint32_t ino;
    int      type;
    char     name[64];
    struct inode *parent, *child, *sibling;
    uint32_t size;
    uint8_t *zdata;
    uint32_t zlen;
    int      zmethod;       /* PACK_LZ / PACK_RAW / PACK_LZH */
    uint8_t *cache;
    uint32_t cap;
    int      opens;
    int      dirty;
    int      orphan;
    uint32_t mtime;
    int      devno;
};

static struct inode *root;
static uint32_t next_ino = 1;

static uint32_t now(void)
{
    return boot_epoch + ticks / HZ;
}

static struct inode *inode_new(const char *name, int type)
{
    struct inode *n = kzalloc(sizeof *n);
    if (!n)
        return 0;
    n->ino = next_ino++;
    n->type = type;
    strlcpy(n->name, name, sizeof n->name);
    n->mtime = now();
    return n;
}

static void inode_free(struct inode *n)
{
    kfree(n->zdata);
    kfree(n->cache);
    kfree(n);
}

void ramfs_init(void)
{
    root = inode_new("", T_DIR);
    root->parent = root;
}

/* ---- paths ---- */
int path_normalize(const char *path, const char *cwd, char *out, size_t n)
{
    char tmp[512];
    size_t len = 0;
    if (path[0] != '/') {
        len = strlcpy(tmp, cwd, sizeof tmp);
        if (len >= sizeof tmp - 1)
            return -ENAMETOOLONG;
        tmp[len++] = '/';
    }
    if (len + strlen(path) >= sizeof tmp)
        return -ENAMETOOLONG;
    strlcpy(tmp + len, path, sizeof tmp - len);

    size_t o = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        char *s = p;
        while (*p && *p != '/')
            p++;
        size_t cl = (size_t)(p - s);
        if (cl == 1 && s[0] == '.')
            continue;
        if (cl == 2 && s[0] == '.' && s[1] == '.') {
            while (o > 0 && out[o - 1] != '/')
                o--;
            if (o > 0)
                o--;
            continue;
        }
        if (cl > 63)
            return -ENAMETOOLONG;
        if (o + cl + 2 >= n)
            return -ENAMETOOLONG;
        out[o++] = '/';
        memcpy(out + o, s, cl);
        o += cl;
    }
    if (o == 0)
        out[o++] = '/';
    out[o] = 0;
    return 0;
}

static struct inode *child_of(struct inode *d, const char *name, size_t nl)
{
    for (struct inode *c = d->child; c; c = c->sibling)
        if (strncmp(c->name, name, nl) == 0 && c->name[nl] == 0)
            return c;
    return 0;
}

static struct inode *walk(const char *norm)
{
    struct inode *n = root;
    const char *p = norm;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *s = p;
        while (*p && *p != '/')
            p++;
        if (n->type != T_DIR)
            return 0;
        n = child_of(n, s, (size_t)(p - s));
        if (!n)
            return 0;
    }
    return n;
}

struct inode *ramfs_lookup(const char *path, const char *cwd)
{
    char norm[256];
    if (path_normalize(path, cwd, norm, sizeof norm) < 0)
        return 0;
    return walk(norm);
}

static void attach(struct inode *dir, struct inode *n)
{
    n->parent = dir;
    n->sibling = dir->child;
    dir->child = n;
    dir->mtime = now();
}

static void detach(struct inode *n)
{
    struct inode **pp = &n->parent->child;
    while (*pp != n)
        pp = &(*pp)->sibling;
    *pp = n->sibling;
    n->sibling = 0;
}

static int split(const char *path, const char *cwd, char *dir, char **name)
{
    int r = path_normalize(path, cwd, dir, 256);
    if (r < 0)
        return r;
    if (dir[1] == 0)
        return -EEXIST;         /* "/" */
    char *slash = dir + strlen(dir);
    while (*slash != '/')
        slash--;
    *name = slash + 1;
    if (slash == dir) {
        memmove(dir + 2, slash + 1, strlen(slash + 1) + 1);
        dir[1] = 0;
        *name = dir + 2;
    } else {
        *slash = 0;
    }
    return 0;
}

struct inode *ramfs_create(const char *path, const char *cwd, int dir, int *err)
{
    char buf[256];
    char *name;
    *err = split(path, cwd, buf, &name);
    if (*err < 0)
        return 0;
    struct inode *d = walk(buf);
    if (!d) { *err = -ENOENT; return 0; }
    if (d->type != T_DIR) { *err = -ENOTDIR; return 0; }
    struct inode *n = child_of(d, name, strlen(name));
    if (n) {
        *err = -EEXIST;
        return n;
    }
    n = inode_new(name, dir ? T_DIR : T_FILE);
    if (!n) { *err = -ENOMEM; return 0; }
    attach(d, n);
    *err = 0;
    return n;
}

int ramfs_unlink(const char *path, const char *cwd)
{
    struct inode *n = ramfs_lookup(path, cwd);
    if (!n)
        return -ENOENT;
    if (n == root)
        return -EBUSY;
    if (n->type == T_DIR && n->child)
        return -ENOTEMPTY;
    detach(n);
    if (n->opens)
        n->orphan = 1;
    else
        inode_free(n);
    return 0;
}

int ramfs_rename(const char *a, const char *b, const char *cwd)
{
    struct inode *n = ramfs_lookup(a, cwd);
    if (!n)
        return -ENOENT;
    if (n == root)
        return -EBUSY;
    char buf[256];
    char *name;
    int r = split(b, cwd, buf, &name);
    if (r < 0)
        return r;
    struct inode *d = walk(buf);
    if (!d)
        return -ENOENT;
    if (d->type != T_DIR)
        return -ENOTDIR;
    for (struct inode *x = d; ; x = x->parent) {
        if (x == n)
            return -EINVAL;     /* moving a dir into itself */
        if (x == root)
            break;
    }
    struct inode *old = child_of(d, name, strlen(name));
    if (old == n)
        return 0;
    if (old) {
        if (old->type == T_DIR)
            return -EISDIR;
        detach(old);
        if (old->opens) old->orphan = 1; else inode_free(old);
    }
    detach(n);
    strlcpy(n->name, name, sizeof n->name);
    attach(d, n);
    return 0;
}

/* ---- file contents ---- */
static int inflate(struct inode *n, uint8_t *dst)
{
    if (!n->size)
        return 0;
    uint8_t *tmp = 0;
    size_t tmpcap = 0;
    if (n->zmethod == PACK_LZH) {
        tmpcap = LZ_BOUND(n->size);
        if (!(tmp = kmalloc(tmpcap)))
            return -ENOMEM;
    }
    long r = lz_unpack(n->zdata, n->zlen, dst, n->size, tmp, tmpcap, n->zmethod);
    kfree(tmp);
    return r == (long)n->size ? 0 : -EIO;
}

int ramfs_open_inode(struct inode *n)
{
    if (n->type == T_FILE && n->opens == 0) {
        n->cap = n->size > 64 ? n->size : 64;
        n->cache = kmalloc(n->cap);
        if (!n->cache)
            return -ENOMEM;
        if (inflate(n, n->cache) < 0) {
            kfree(n->cache);
            n->cache = 0;
            return -EIO;
        }
        n->dirty = 0;
    }
    n->opens++;
    return 0;
}

static void deflate(struct inode *n)
{
    kfree(n->zdata);
    n->zdata = 0;
    n->zlen = 0;
    n->zmethod = PACK_RAW;
    if (!n->size) {
        kfree(n->cache);
        n->cache = 0;
        return;
    }
    uint32_t bound = LZ_BOUND(n->size);
    uint8_t *blob = kmalloc(bound), *tmp = kmalloc(bound);
    int method = PACK_RAW;
    size_t clen = blob && tmp ? lz_pack(n->cache, n->size, blob, bound, tmp, LZ_LEVEL_HEAVY, &method) : 0;
    kfree(tmp);
    if (clen && method != PACK_RAW) {
        n->zdata = krealloc(blob, clen);
        n->zlen = (uint32_t)clen;
        n->zmethod = method;
        kfree(n->cache);
    } else {
        kfree(blob);
        n->zdata = krealloc(n->cache, n->size);
        n->zlen = n->size;
    }
    n->cache = 0;
}

void ramfs_close_inode(struct inode *n)
{
    if (--n->opens > 0)
        return;
    if (n->type == T_FILE) {
        if (n->dirty)
            deflate(n);
        else {
            kfree(n->cache);
            n->cache = 0;
        }
        n->dirty = 0;
    }
    if (n->orphan)
        inode_free(n);
}

static int reserve(struct inode *n, uint32_t len)
{
    if (len <= n->cap)
        return 0;
    uint32_t c = n->cap * 2;
    if (c < len)
        c = len;
    uint8_t *p = krealloc(n->cache, c);
    if (!p)
        return -ENOSPC;
    n->cache = p;
    n->cap = c;
    return 0;
}

/* buf may be a user pointer: faults are serviced transparently */
int ramfs_rw(struct inode *n, uint32_t off, void *buf, uint32_t len, int write)
{
    if (n->type != T_FILE)
        return -EISDIR;
    if (!write) {
        if (off >= n->size)
            return 0;
        if (len > n->size - off)
            len = n->size - off;
        memcpy(buf, n->cache + off, len);
        return (int)len;
    }
    if (off + len < off)
        return -EFBIG;
    int r = reserve(n, off + len);
    if (r < 0)
        return r;
    if (off > n->size)
        memset(n->cache + n->size, 0, off - n->size);
    memcpy(n->cache + off, buf, len);
    if (off + len > n->size)
        n->size = off + len;
    n->dirty = 1;
    n->mtime = now();
    return (int)len;
}

int ramfs_truncate(struct inode *n, uint32_t len)
{
    if (n->type != T_FILE)
        return -EISDIR;
    int r = reserve(n, len);
    if (r < 0)
        return r;
    if (len > n->size)
        memset(n->cache + n->size, 0, len - n->size);
    n->size = len;
    n->dirty = 1;
    n->mtime = now();
    return 0;
}

uint8_t *ramfs_read_all(struct inode *n, uint32_t *len)
{
    uint8_t *b = kmalloc(n->size ? n->size : 1);
    if (!b)
        return 0;
    if (n->cache)
        memcpy(b, n->cache, n->size);
    else if (inflate(n, b) < 0) {
        kfree(b);
        return 0;
    }
    *len = n->size;
    return b;
}

int ramfs_mknod(const char *path, int devno)
{
    int err;
    struct inode *n = ramfs_create(path, "/", 0, &err);
    if (!n)
        return err;
    n->type = T_DEV;
    n->devno = devno;
    return 0;
}

int ramfs_devno(struct inode *n)
{
    return n->type == T_DEV ? n->devno : -1;
}

int ramfs_isdir(struct inode *n)
{
    return n->type == T_DIR;
}

static uint32_t held(struct inode *n)
{
    return n->cache ? n->cap : n->zlen;
}

void ramfs_stat(struct inode *n, struct kiwi_stat *st)
{
    st->ino = n->ino;
    st->mode = n->type == T_DIR ? S_IFDIR | 0755 : n->type == T_DEV ? S_IFCHR | 0666 : S_IFREG | 0644;
    st->size = n->size;
    st->csize = held(n);
    st->nlink = 1;
    st->mtime = n->mtime;
}

int ramfs_readdir(struct inode *d, uint32_t idx, struct kiwi_dirent *de)
{
    if (d->type != T_DIR)
        return -ENOTDIR;
    struct inode *c = d->child;
    while (c && idx--)
        c = c->sibling;
    if (!c)
        return 0;
    de->d_ino = c->ino;
    de->d_type = c->type == T_DIR ? DT_DIR : c->type == T_DEV ? DT_CHR : DT_REG;
    de->d_size = c->size;
    de->d_csize = held(c);
    strlcpy(de->d_name, c->name, sizeof de->d_name);
    return 1;
}

int ramfs_path(struct inode *n, char *buf, size_t len)
{
    char tmp[256];
    size_t o = sizeof tmp - 1;
    tmp[o] = 0;
    for (; n != root; n = n->parent) {
        size_t l = strlen(n->name);
        if (l + 1 > o)
            return -ENAMETOOLONG;
        o -= l;
        memcpy(tmp + o, n->name, l);
        tmp[--o] = '/';
    }
    if (tmp[o] == 0)
        tmp[--o] = '/';
    strlcpy(buf, tmp + o, len);
    return 0;
}

static void totals(struct inode *n, uint32_t *f, uint32_t *b, uint32_t *z)
{
    for (struct inode *c = n->child; c; c = c->sibling) {
        if (c->type == T_DIR) {
            totals(c, f, b, z);
        } else {
            (*f)++;
            *b += c->size;
            *z += held(c);
        }
    }
}

void ramfs_totals(uint32_t *files, uint32_t *bytes, uint32_t *zbytes)
{
    *files = *bytes = *zbytes = 0;
    totals(root, files, bytes, zbytes);
}

/* ---- initrd ---- */
static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ramfs_load_initrd(const uint8_t *img, uint32_t len)
{
    if (len < 8 || memcmp(img, "KIRD", 4) != 0) {
        kprintf("initrd: bad image\n");
        return;
    }
    uint32_t count = rd32(img + 4), off = 8;
    uint32_t raw = 0, packed = 0;
    for (uint32_t i = 0; i < count && off + 12 <= len; i++) {
        const uint8_t *e = img + off;
        int type = e[0], method = e[1];
        uint32_t plen = e[2] | (e[3] << 8);
        uint32_t size = rd32(e + 4), csize = rd32(e + 8);
        char path[256];
        if (plen >= sizeof path || off + 12 + plen + csize > len)
            break;
        memcpy(path, e + 12, plen);
        path[plen] = 0;
        int err;
        struct inode *n = ramfs_create(path, "/", type == T_DIR, &err);
        if (n && type == T_FILE && csize) {
            n->zdata = kmalloc(csize);
            if (!n->zdata)
                panic("initrd: out of memory");
            memcpy(n->zdata, e + 12 + plen, csize);
            n->zlen = csize;
            n->zmethod = method;
            n->size = size;
            raw += size;
            packed += csize;
        }
        off += 12 + plen + csize;
    }
    kprintf("initrd: %d entries, %d KB of files held in %d KB (%d%%)\n",
            count, raw / 1024, packed / 1024, raw ? packed * 100 / raw : 0);
}
