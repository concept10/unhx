/*
 * servers/vfs/vfs_msg.h — VFS server IPC message definitions for UNHOX
 *
 * The VFS server runs as a kernel thread, registers as "com.unhox.vfs" with
 * the bootstrap server, and serves open/read/close requests via Mach IPC.
 *
 * Message flow:
 *   client → VFS_MSG_OPEN  → vfs_server → VFS_MSG_REPLY (fd or error)
 *   client → VFS_MSG_READ  → vfs_server → VFS_MSG_REPLY (data or error)
 *   client → VFS_MSG_CLOSE → vfs_server (no reply)
 *
 * Reference: OSF MK servers/vfs/vfs_server.h for the original interface.
 */

#ifndef VFS_MSG_H
#define VFS_MSG_H

#include <stdint.h>
#include "mach/mach_types.h"

/* Forward declaration */
struct ipc_port;

/* VFS message IDs */
#define VFS_MSG_OPEN    200
#define VFS_MSG_READ    201
#define VFS_MSG_CLOSE   202
#define VFS_MSG_WRITE   203
#define VFS_MSG_STAT    204
#define VFS_MSG_READDIR 206
#define VFS_MSG_MKDIR   207
#define VFS_MSG_UNLINK  208
#define VFS_MSG_REPLY   205

/* Path and data size limits */
#define VFS_PATH_MAX    128
#define VFS_DATA_MAX    256

/* Return codes */
#define VFS_SUCCESS     0
#define VFS_NOT_FOUND   1
#define VFS_BAD_FD      2
#define VFS_NO_MEMORY   3

/* Global VFS port — set by vfs_server_main() before entering the loop */
extern struct ipc_port *vfs_port;

/* -------------------------------------------------------------------------
 * Message structures
 * ------------------------------------------------------------------------- */

/*
 * vfs_open_msg_t — open a file by path.
 * Client sends to vfs_port->ip_messages.
 * Server replies to reply_port with vfs_reply_msg_t (result = fd).
 */
typedef struct {
    mach_msg_header_t   hdr;
    char                path[VFS_PATH_MAX];
    uint64_t            reply_port;     /* ipc_port * for reply */
} vfs_open_msg_t;

/*
 * vfs_read_msg_t — read bytes from an open file.
 * Server replies with vfs_reply_msg_t (result = bytes read, data = content).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    uint32_t            count;          /* bytes requested (max VFS_DATA_MAX) */
    uint32_t            offset;         /* byte offset within file */
    uint32_t            _pad;
    uint64_t            reply_port;
} vfs_read_msg_t;

/*
 * vfs_close_msg_t — close a file descriptor (fire-and-forget, no reply).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    int32_t             _pad;
} vfs_close_msg_t;

/*
 * vfs_write_msg_t — write bytes to a file.
 * Server replies with vfs_reply_msg_t (result = bytes written or error).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    uint32_t            count;          /* bytes to write */
    uint8_t             data[VFS_DATA_MAX];
    uint64_t            reply_port;
} vfs_write_msg_t;

/*
 * vfs_stat_msg_t — get file metadata.
 * Server replies with vfs_reply_msg_t (result = size, retcode = success).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    int32_t             _pad;
    uint64_t            reply_port;
} vfs_stat_msg_t;

/*
 * vfs_readdir_msg_t — list directory contents.
 * Server replies with vfs_reply_msg_t (data = directory entries).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    int32_t             _pad;
    uint64_t            reply_port;
} vfs_readdir_msg_t;

/*
 * vfs_mkdir_msg_t — create a directory.
 * Server replies with vfs_reply_msg_t (result = fd of new dir or error).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             _pad;
    uint32_t            mode;
    uint8_t             path[VFS_PATH_MAX];
    uint64_t            reply_port;
} vfs_mkdir_msg_t;

/*
 * vfs_unlink_msg_t — delete a file or directory.
 * Server replies with vfs_reply_msg_t (retcode = success or error).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             _pad1;
    int32_t             _pad2;
    uint8_t             path[VFS_PATH_MAX];
    uint64_t            reply_port;
} vfs_unlink_msg_t;

/*
 * vfs_reply_msg_t — reply from VFS server to client.
 *
 * For VFS_MSG_OPEN:    retcode, result = fd (>= 0), data unused.
 * For VFS_MSG_READ:    retcode, result = bytes read, data = file content.
 * For VFS_MSG_WRITE:   retcode, result = bytes written.
 * For VFS_MSG_STAT:    retcode, result = file size.
 * For VFS_MSG_READDIR: retcode, result = entry count, data = dir entries.
 * For VFS_MSG_MKDIR:   retcode, result = fd or error.
 * For VFS_MSG_UNLINK:  retcode, result = 0 or error.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             retcode;        /* VFS_SUCCESS or error code */
    int32_t             result;         /* fd, byte count, size, or status */
    uint8_t             data[VFS_DATA_MAX];
} vfs_reply_msg_t;

/* -------------------------------------------------------------------------
 * Server entry point
 * ------------------------------------------------------------------------- */

void vfs_server_main(void);

#endif /* VFS_MSG_H */
