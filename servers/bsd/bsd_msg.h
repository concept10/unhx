/*
 * servers/bsd/bsd_msg.h — BSD server IPC message definitions for UNHOX
 *
 * The BSD server runs as a kernel thread registered as "com.unhox.bsd" with
 * the bootstrap server. It maintains a per-process file descriptor table and
 * routes fd 0/1/2 to the serial console.
 *
 * For Phase 2, file descriptor numbers map directly:
 *   fd 0  — serial input  (SYS_READ)
 *   fd 1  — serial output (SYS_WRITE)
 *   fd 2  — serial output (SYS_WRITE, stderr)
 *
 * Reference: OSF MK servers/bsd/bsd_server.h
 */

#ifndef BSD_MSG_H
#define BSD_MSG_H

#include <stdint.h>
#include "mach/mach_types.h"

/* Forward declaration */
struct ipc_port;
struct interrupt_frame;

/* BSD message IDs */
#define BSD_MSG_WRITE   300
#define BSD_MSG_READ    301
#define BSD_MSG_REPLY   302

/* Data payload limit per message */
#define BSD_DATA_MAX    256

/* Return codes */
#define BSD_SUCCESS     0
#define BSD_BAD_FD      1
#define BSD_IO_ERROR    2

/* Global BSD port — set by bsd_server_main() before entering the loop */
extern struct ipc_port *bsd_port;

/* -------------------------------------------------------------------------
 * Message structures
 * ------------------------------------------------------------------------- */

/*
 * bsd_write_msg_t — write data to a file descriptor.
 * Fire-and-forget; no reply is sent.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    uint32_t            count;
    uint8_t             data[BSD_DATA_MAX];
} bsd_write_msg_t;

/*
 * bsd_read_msg_t — read from a file descriptor.
 * Server replies to reply_port with bsd_reply_msg_t.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             fd;
    uint32_t            count;          /* bytes requested */
    uint64_t            reply_port;     /* ipc_port * for reply */
} bsd_read_msg_t;

/*
 * bsd_reply_msg_t — reply from BSD server to client.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             retcode;        /* BSD_SUCCESS or error */
    int32_t             count;          /* bytes read */
    uint8_t             data[BSD_DATA_MAX];
} bsd_reply_msg_t;

/* -------------------------------------------------------------------------
 * Server entry point
 * ------------------------------------------------------------------------- */

void bsd_server_main(void);

/*
 * bsd_exec_current — replace current user task image with an ELF loaded
 * from ramfs via the VFS server, then redirect syscall return frame.
 *
 * Returns 0 on success, -1 on failure.
 */
int bsd_exec_current(const char *path, struct interrupt_frame *frame);

#endif /* BSD_MSG_H */
