/*
 * servers/bsd/fd_table.c — File descriptor table for NEOMACH BSD server
 *
 * Implements fd_table operations: init, alloc, close, dup, dup2, copy.
 * See fd_table.h for the design rationale.
 */

#include "fd_table.h"
#include "bsd_proto.h"
#include "kern/klib.h"

/* =========================================================================
 * fd_table_init
 * ========================================================================= */

void fd_table_init(struct fd_table *fdt)
{
    if (!fdt)
        return;

    kmemset(fdt, 0, sizeof(struct fd_table));

    /*
     * Pre-allocate the three standard streams as stubs.
     *
     * In a real system stdin/stdout/stderr would be connected to a terminal
     * device through the VFS server.  For Phase 2 we mark them open with
     * read+write flags and a null VFS port; the fd numbers are reserved so
     * that new allocations start from fd 3.
     */
    fdt->fdt_entries[STDIN_FILENO].fe_open     = 1;
    fdt->fdt_entries[STDIN_FILENO].fe_flags    = O_RDONLY;
    fdt->fdt_entries[STDIN_FILENO].fe_vfs_port = MACH_PORT_NULL;
    fdt->fdt_entries[STDIN_FILENO].fe_offset   = 0;

    fdt->fdt_entries[STDOUT_FILENO].fe_open     = 1;
    fdt->fdt_entries[STDOUT_FILENO].fe_flags    = O_WRONLY;
    fdt->fdt_entries[STDOUT_FILENO].fe_vfs_port = MACH_PORT_NULL;
    fdt->fdt_entries[STDOUT_FILENO].fe_offset   = 0;

    fdt->fdt_entries[STDERR_FILENO].fe_open     = 1;
    fdt->fdt_entries[STDERR_FILENO].fe_flags    = O_WRONLY;
    fdt->fdt_entries[STDERR_FILENO].fe_vfs_port = MACH_PORT_NULL;
    fdt->fdt_entries[STDERR_FILENO].fe_offset   = 0;

    fdt->fdt_nextfd = STDERR_FILENO + 1;
}

/* =========================================================================
 * fd_alloc
 * ========================================================================= */

int fd_alloc(struct fd_table *fdt, uint32_t flags, mach_port_name_t vfs_port)
{
    uint32_t start;
    int fd;

    if (!fdt)
        return -1;

    /*
     * POSIX requires the lowest-available fd.  We scan from fdt_nextfd as
     * a hint, then wrap around from 0 if needed.
     */
    start = fdt->fdt_nextfd;
    if (start >= FD_MAX)
        start = 0;

    for (fd = (int)start; fd < FD_MAX; fd++) {
        if (!fdt->fdt_entries[fd].fe_open)
            goto found;
    }
    for (fd = 0; fd < (int)start; fd++) {
        if (!fdt->fdt_entries[fd].fe_open)
            goto found;
    }
    return -1;  /* table full */

found:
    fdt->fdt_entries[fd].fe_open     = 1;
    fdt->fdt_entries[fd].fe_flags    = flags;
    fdt->fdt_entries[fd].fe_vfs_port = vfs_port;
    fdt->fdt_entries[fd].fe_offset   = 0;

    /* Advance the hint past this slot */
    fdt->fdt_nextfd = (uint32_t)(fd + 1 < FD_MAX ? fd + 1 : 0);
    return fd;
}

/* =========================================================================
 * fd_close
 * ========================================================================= */

int fd_close(struct fd_table *fdt, int fd)
{
    if (!fdt || fd < 0 || fd >= FD_MAX)
        return BSD_EBADF;
    if (!fdt->fdt_entries[fd].fe_open)
        return BSD_EBADF;

    /*
     * Phase 3+: if fe_vfs_port != MACH_PORT_NULL, send a close message to
     * the VFS server and deallocate the send right with ipc_right_deallocate.
     */

    kmemset(&fdt->fdt_entries[fd], 0, sizeof(struct fd_entry));

    /* Update the hint if this fd is below the current next-free */
    if ((uint32_t)fd < fdt->fdt_nextfd)
        fdt->fdt_nextfd = (uint32_t)fd;

    return BSD_ESUCCESS;
}

/* =========================================================================
 * fd_dup
 * ========================================================================= */

int fd_dup(struct fd_table *fdt, int fd)
{
    struct fd_entry *src;
    int newfd;

    if (!fdt || fd < 0 || fd >= FD_MAX)
        return -1;

    src = &fdt->fdt_entries[fd];
    if (!src->fe_open)
        return -1;

    newfd = fd_alloc(fdt, src->fe_flags, src->fe_vfs_port);
    if (newfd < 0)
        return -1;

    /* The new fd inherits the offset too (open file description sharing) */
    fdt->fdt_entries[newfd].fe_offset = src->fe_offset;

    /*
     * POSIX: dup() clears O_CLOEXEC on the new descriptor.
     * The original descriptor's O_CLOEXEC is not affected.
     */
    fdt->fdt_entries[newfd].fe_flags &= (uint32_t)~O_CLOEXEC;

    /*
     * Phase 3+: addref on fe_vfs_port so the VFS server knows two fds
     * reference the same open file description.
     */

    return newfd;
}

/* =========================================================================
 * fd_dup2
 * ========================================================================= */

int fd_dup2(struct fd_table *fdt, int fd, int newfd)
{
    struct fd_entry *src;

    if (!fdt || fd < 0 || fd >= FD_MAX || newfd < 0 || newfd >= FD_MAX)
        return -1;

    src = &fdt->fdt_entries[fd];
    if (!src->fe_open)
        return -1;

    /* dup2(fd, fd) is a no-op */
    if (fd == newfd)
        return newfd;

    /* Silently close newfd if it is open */
    if (fdt->fdt_entries[newfd].fe_open)
        fd_close(fdt, newfd);

    fdt->fdt_entries[newfd].fe_open     = 1;
    fdt->fdt_entries[newfd].fe_flags    = src->fe_flags & (uint32_t)~O_CLOEXEC;
    fdt->fdt_entries[newfd].fe_vfs_port = src->fe_vfs_port;
    fdt->fdt_entries[newfd].fe_offset   = src->fe_offset;

    return newfd;
}

/* =========================================================================
 * fd_get
 * ========================================================================= */

struct fd_entry *fd_get(struct fd_table *fdt, int fd)
{
    if (!fdt || fd < 0 || fd >= FD_MAX)
        return (void *)0;
    if (!fdt->fdt_entries[fd].fe_open)
        return (void *)0;
    return &fdt->fdt_entries[fd];
}

/* =========================================================================
 * fd_table_copy
 * ========================================================================= */

void fd_table_copy(struct fd_table *dst, const struct fd_table *src)
{
    if (!dst || !src)
        return;

    kmemcpy(dst, src, sizeof(struct fd_table));

    /*
     * Phase 3+: For every open entry that has a non-null fe_vfs_port,
     * send an addref message to the VFS server so it knows the child
     * holds an additional reference to the open file description.
     */
}

/* =========================================================================
 * fd_table_cloexec
 * ========================================================================= */

void fd_table_cloexec(struct fd_table *fdt)
{
    int fd;

    if (!fdt)
        return;

    for (fd = 0; fd < FD_MAX; fd++) {
        if (fdt->fdt_entries[fd].fe_open &&
            (fdt->fdt_entries[fd].fe_flags & O_CLOEXEC)) {
            fd_close(fdt, fd);
        }
    }
}

/* =========================================================================
 * fd_table_close_all
 * ========================================================================= */

void fd_table_close_all(struct fd_table *fdt)
{
    int fd;

    if (!fdt)
        return;

    for (fd = 0; fd < FD_MAX; fd++) {
        if (fdt->fdt_entries[fd].fe_open)
            fd_close(fdt, fd);
    }
}
