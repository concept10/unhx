/*
 * servers/bsd/fd_table.h — File descriptor table for NEOMACH BSD server
 *
 * Each BSD process has a file descriptor table: an array mapping small
 * non-negative integers (file descriptors) to open-file descriptions.
 *
 * FILE DESCRIPTOR MODEL:
 *
 *   In a standard UNIX kernel the open-file description (struct file) is
 *   reference-counted and shared between file descriptors created by dup()
 *   and fork().  The file descriptor table (struct fdtable) is per-process
 *   but the underlying open-file descriptions are shared.
 *
 *   In the NEOMACH multi-server design:
 *
 *     - The BSD server owns the per-process fd table (this file).
 *     - Each fd entry holds a send right to the VFS server port for that
 *       open file (the VFS server maintains the open-file description).
 *     - Operations on an fd (read, write, lseek, fstat) are Mach RPCs sent
 *       to that port.
 *     - dup() copies the send right; the VFS server's reference count on the
 *       open file goes up by one.
 *     - close() deallocates the send right; the VFS server's refcount drops.
 *
 *   Phase 2 NOTE:
 *     The VFS server and the port-per-file scheme require OOL memory and
 *     port right transfer in messages — features targeted for Phase 3.
 *     For Phase 2 we implement the fd table data structure and all fd
 *     management operations (alloc, dup, close, copy-on-fork), but the
 *     backing port field is a stub (0 = not connected to real VFS).
 *
 * Reference: Stevens, "Advanced Programming in the UNIX Environment" §3;
 *            GNU HURD hurd/fd.h for the port-per-fd design.
 */

#ifndef BSD_FD_TABLE_H
#define BSD_FD_TABLE_H

#include "mach/mach_types.h"
#include <stdint.h>

/* =========================================================================
 * Limits
 * ========================================================================= */

/*
 * FD_MAX — maximum open file descriptors per process.
 *
 * Phase 2: fixed at 64 to keep the static structure small.
 * Phase 3+: dynamic growth backed by kalloc().
 *
 * POSIX requires at least OPEN_MAX (20 minimum; common systems use 1024+).
 */
#define FD_MAX          64

/* Reserved file descriptors (stdin / stdout / stderr) */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* =========================================================================
 * Open flags (subset of POSIX fcntl.h O_* flags)
 * ========================================================================= */

#define O_RDONLY    0x0000  /* open for reading only                        */
#define O_WRONLY    0x0001  /* open for writing only                        */
#define O_RDWR      0x0002  /* open for reading and writing                 */
#define O_ACCMODE   0x0003  /* mask for access mode bits                    */
#define O_APPEND    0x0400  /* writes always append to end of file          */
#define O_NONBLOCK  0x0800  /* non-blocking I/O                             */
#define O_CLOEXEC   0x0040  /* close-on-exec flag                           */

/* =========================================================================
 * struct fd_entry — one slot in the file descriptor table
 * ========================================================================= */

struct fd_entry {
    /*
     * fe_open — 1 if this slot is in use (fd is open), 0 if free.
     */
    int                 fe_open;

    /*
     * fe_flags — combination of O_* flags for this open file.
     */
    uint32_t            fe_flags;

    /*
     * fe_vfs_port — send right to the VFS server port for the open file.
     *
     * Phase 2 stub: always 0 (MACH_PORT_NULL) since VFS is not yet running.
     * Phase 3+: populated by bsd_open() after sending BSD_MSG_OPEN to the
     *           VFS server and receiving back the per-file port.
     */
    mach_port_name_t    fe_vfs_port;

    /*
     * fe_offset — current file offset (bytes from start of file).
     *
     * In the full design this lives in the VFS open-file description and is
     * updated by the VFS server.  We cache it here so that sequential read/
     * write requests can avoid an extra round-trip for the common case.
     *
     * Phase 2: initialised to 0 and not advanced (no real I/O yet).
     */
    uint64_t            fe_offset;
};

/* =========================================================================
 * struct fd_table — per-process file descriptor table
 * ========================================================================= */

struct fd_table {
    struct fd_entry     fdt_entries[FD_MAX];

    /*
     * fdt_nextfd — hint for fd_alloc(): start searching from this index.
     * Choosing the lowest-available fd (POSIX requirement) is O(n) with a
     * linear scan; this hint avoids scanning already-allocated entries in
     * the common case of sequential open/close.
     */
    uint32_t            fdt_nextfd;
};

/* =========================================================================
 * fd_table operations
 * ========================================================================= */

/*
 * fd_table_init — initialise a file descriptor table.
 *
 * Marks all entries as closed.  The three standard streams (stdin/stdout/
 * stderr) are pre-allocated as stubs pointing to MACH_PORT_NULL; real VFS
 * connections are made later by the BSD server init sequence.
 */
void fd_table_init(struct fd_table *fdt);

/*
 * fd_alloc — allocate the lowest-available file descriptor number.
 *
 * flags:    the O_* flags for the new file description
 * vfs_port: send right to the VFS server for this open file
 *           (MACH_PORT_NULL if not yet connected to VFS)
 *
 * Returns the allocated fd (≥ 0) on success, or −1 if the table is full.
 */
int fd_alloc(struct fd_table *fdt, uint32_t flags, mach_port_name_t vfs_port);

/*
 * fd_close — close file descriptor fd.
 *
 * Marks the slot as free and (Phase 3+) releases the VFS send right.
 * Returns 0 on success, BSD_EBADF if fd is not open.
 */
int fd_close(struct fd_table *fdt, int fd);

/*
 * fd_dup — duplicate fd, placing the duplicate at the lowest available slot.
 *
 * Returns the new fd (≥ 0) on success, −1 on failure.
 * Both the original and duplicate share fe_vfs_port (Phase 3+: addref on port).
 */
int fd_dup(struct fd_table *fdt, int fd);

/*
 * fd_dup2 — duplicate fd into slot newfd.
 *
 * If newfd is already open it is silently closed first (POSIX dup2 semantics).
 * Returns newfd on success, −1 on failure.
 */
int fd_dup2(struct fd_table *fdt, int fd, int newfd);

/*
 * fd_get — retrieve the entry for fd.
 *
 * Returns a pointer to the entry on success (caller must not free it),
 * or NULL if fd is out of range or not open.
 */
struct fd_entry *fd_get(struct fd_table *fdt, int fd);

/*
 * fd_table_copy — copy a file descriptor table (used by fork()).
 *
 * In POSIX, fork() produces a child with a copy of the parent's fd table.
 * All open file descriptions are shared, so each fd in the child gets
 * the same VFS port (Phase 3+: reference count on the port is incremented).
 *
 * The O_CLOEXEC flag is NOT cleared here; exec() is responsible for closing
 * those descriptors.
 */
void fd_table_copy(struct fd_table *dst, const struct fd_table *src);

/*
 * fd_table_cloexec — close all file descriptors with O_CLOEXEC set.
 * Called during exec() processing.
 */
void fd_table_cloexec(struct fd_table *fdt);

/*
 * fd_table_close_all — close every open file descriptor.
 * Called during process exit.
 */
void fd_table_close_all(struct fd_table *fdt);

#endif /* BSD_FD_TABLE_H */
