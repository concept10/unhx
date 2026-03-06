# servers/

Userspace personality servers — the kernel/server split.

Unlike XNU, UNHOX maintains true microkernel discipline. Everything above the Mach
primitives lives in userspace servers communicating via Mach IPC.

## Servers

| Server         | Responsibility |
|---------------|----------------|
| `bsd/`        | POSIX/BSD personality — fork, exec, signals, file descriptors |
| `vfs/`        | Virtual filesystem — translator model (HURD-inspired) |
| `device/`     | Hardware device abstraction and driver management |
| `network/`    | TCP/IP network stack (lwIP or picoTCP initially) |
| `auth/`       | Capability-based authentication (port right delegation) |
| `display/`    | Display server — Mach-IPC-native compositor with X11/Wayland personalities |

## Design Notes

Each server registers itself with the bootstrap server at boot and receives a
well-known port name. Clients obtain server ports through the bootstrap server.

The BSD server is the most complex — it must emulate POSIX semantics (signals,
fork, blocking syscalls) across process boundaries. See `docs/bsd-server-design.md`.

## Phase 2 Deliverable

A userspace shell (`/bin/sh`) running on UNHOX, communicating with the BSD server
for process and file descriptor management.
