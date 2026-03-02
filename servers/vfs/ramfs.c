/*
 * servers/vfs/ramfs.c — In-memory filesystem for UNHOX
 *
 * Pre-populated with a small set of static files. Used by the VFS server
 * to serve VFS_MSG_OPEN / VFS_MSG_READ requests.
 */

#include "vfs/ramfs.h"
#include "kern/klib.h"

struct ramfs_file {
    char     name[RAMFS_PATH_MAX];
    uint8_t  data[RAMFS_DATA_MAX];
    uint32_t size;
    int      active;
};

static struct ramfs_file files[RAMFS_MAX_FILES];
static int nfiles = 0;

/* -------------------------------------------------------------------------
 * Internal helper — add a file to the store
 * ------------------------------------------------------------------------- */

static void ramfs_add(const char *name, const char *content, uint32_t size)
{
    if (nfiles >= RAMFS_MAX_FILES)
        return;

    struct ramfs_file *f = &files[nfiles];
    kstrncpy(f->name, name, RAMFS_PATH_MAX);

    if (size > RAMFS_DATA_MAX)
        size = RAMFS_DATA_MAX;
    kmemcpy(f->data, content, size);
    f->size   = size;
    f->active = 1;
    nfiles++;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void ramfs_init(void)
{
    kmemset(files, 0, sizeof(files));
    nfiles = 0;

    ramfs_add("/test.txt",
              "Hello from ramfs!\n",
              18);

    ramfs_add("/bin/README",
              "UNHOX /bin — shell and system utilities\n",
              40);
}

int ramfs_open(const char *path)
{
    if (!path)
        return -1;

    for (int i = 0; i < nfiles; i++) {
        if (files[i].active && kstrcmp(files[i].name, path) == 0)
            return i;
    }
    return -1;
}

int ramfs_read(int fd, void *buf, uint32_t count, uint32_t offset)
{
    if (fd < 0 || fd >= nfiles || !files[fd].active)
        return -1;

    struct ramfs_file *f = &files[fd];
    if (offset >= f->size)
        return 0;   /* EOF */

    uint32_t avail = f->size - offset;
    if (count > avail)
        count = avail;

    kmemcpy(buf, f->data + offset, count);
    return (int)count;
}

int ramfs_close(int fd)
{
    (void)fd;
    return 0;
}

int ramfs_write(int fd, const void *buf, uint32_t count)
{
    if (fd < 0 || fd >= nfiles || !files[fd].active)
        return -1;

    /* For Phase 2: ramfs is read-only, write returns 0 (silent success) */
    (void)buf;
    (void)count;
    return 0;
}

int ramfs_stat(int fd, uint32_t *size_out)
{
    if (fd < 0 || fd >= nfiles || !files[fd].active)
        return -1;

    if (size_out)
        *size_out = files[fd].size;
    return 0;
}

int ramfs_size(int fd)
{
    if (fd < 0 || fd >= nfiles || !files[fd].active)
        return -1;
    return (int)files[fd].size;
}
