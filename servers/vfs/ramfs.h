/*
 * servers/vfs/ramfs.h — In-memory filesystem for UNHOX
 *
 * A simple flat-file store backed by static arrays. Files are added at init
 * time and are read-only. The fd returned by ramfs_open() is an index into
 * the file table — no per-open state is maintained.
 */

#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

#define RAMFS_MAX_FILES 16
#define RAMFS_PATH_MAX  128
#define RAMFS_DATA_MAX  512

/* Populate the ramfs with pre-defined files */
void ramfs_init(void);

/* Open a file by path. Returns fd >= 0 on success, -1 if not found. */
int ramfs_open(const char *path);

/*
 * Read up to `count` bytes from file `fd` starting at `offset`.
 * Returns bytes copied, 0 at EOF, -1 on bad fd.
 */
int ramfs_read(int fd, void *buf, uint32_t count, uint32_t offset);

/* Release an fd (no-op in this implementation). */
int ramfs_close(int fd);

/* Return the size of a file, or -1 on bad fd. */
int ramfs_size(int fd);

#endif /* RAMFS_H */
