/*
 * kernel/ipc/ipc.h — Mach IPC subsystem for UNHOX
 *
 * The IPC subsystem implements Mach ports and message passing.  It is the
 * most fundamental part of the kernel: every cross-component interaction,
 * whether between two userspace servers or between a server and the kernel,
 * goes through port IPC.
 *
 * Subsystem components:
 *   ipc_port.h  / ipc_port.c  — kernel-internal port objects
 *   ipc_space.h / ipc_space.c — per-task port name spaces
 *   ipc_entry.h               — individual name-space entries
 *   ipc_mqueue.h/ ipc_mqueue.c— per-port message queues (Phase 2)
 *   ipc_kmsg.h  / ipc_kmsg.c  — mach_msg() kernel entry point (Phase 2)
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC.
 */

#ifndef IPC_H
#define IPC_H

#include "mach/mach_types.h"
#include "ipc_port.h"
#include "ipc_space.h"
#include "ipc_entry.h"

/*
 * ipc_init — initialise the IPC subsystem.
 * Called once during kernel startup before any tasks are created.
 */
void ipc_init(void);

#endif /* IPC_H */
