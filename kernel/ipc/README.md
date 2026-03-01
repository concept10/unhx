# kernel/ipc/

Mach port IPC implementation.

## Overview

The IPC subsystem is the heart of the Mach microkernel. Ports are first-class
capability-like objects — a port right is authority. All cross-domain communication
flows through port messages.

## Key Abstractions

- **Mach Port** — a message queue with associated rights
- **Port Right** — SEND, RECEIVE, SEND_ONCE, PORT_SET, DEAD_NAME
- **Mach Message** — structured data + out-of-line memory + port rights
- **Port Set** — a receive set for multiplexing multiple ports

## Implementation Plan

- [ ] `ipc_port.h` / `ipc_port.c` — port structure and lifecycle
- [ ] `ipc_right.h` / `ipc_right.c` — right management in task IPC space
- [ ] `ipc_mqueue.h` / `ipc_mqueue.c` — message queue with blocking
- [ ] `ipc_kmsg.h` / `ipc_kmsg.c` — kernel message allocation and copy
- [ ] `mach_msg.c` — `mach_msg()` trap implementation
- [ ] `ipc_space.c` — per-task IPC name space (port name → right mapping)

## References

- Mach 3.0 Kernel Principles (CMU TR)
- GNU Mach `ipc/` directory
- XNU `osfmk/ipc/`
