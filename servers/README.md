# servers/

Userspace personality servers — the kernel/server split.

Unlike XNU, NEOMACH maintains true microkernel discipline. Everything above the Mach
primitives lives in userspace servers communicating via Mach IPC.

## Servers

| Server         | Status | Responsibility |
|---------------|--------|----------------|
| `bootstrap/`  | ✅ Phase 1 | Bootstrap server — service registration and port lookup |
| `bsd/`        | 🔲 Phase 2 | POSIX/BSD personality — fork, exec, signals, file descriptors |
| `vfs/`        | 🔲 Phase 2 | Virtual filesystem — translator model (HURD-inspired) |
| `device/`     | 🔲 Phase 3 | Hardware device abstraction and driver management |
| `network/`    | 🔲 Phase 3 | TCP/IP network stack (lwIP or picoTCP initially) |
| `auth/`       | 🔲 Phase 3 | Capability-based authentication (port right delegation) |
| `display/`    | 🔲 Phase 5 | Display server — Mach-IPC-native compositor with X11/Wayland personalities |

## Current Status

**Phase 1 complete:** The bootstrap server (`bootstrap/`) is implemented.
It registers services via name→port mappings and is called during kernel
startup. See [HISTORY.md](../HISTORY.md) for the first-boot log.

**Phase 2 next:** BSD server, VFS server, and shell.

## Design Notes

Each server registers itself with the bootstrap server at boot and receives a
well-known port name. Clients obtain server ports through the bootstrap server.

The BSD server is the most complex — it must emulate POSIX semantics (signals,
fork, blocking syscalls) across process boundaries. See `docs/bsd-server-design.md`.

## Phase 2 Deliverable

A userspace shell (`/bin/sh`) running on NEOMACH, communicating with the BSD server
for process and file descriptor management.
