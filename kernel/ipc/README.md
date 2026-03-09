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

## Current Status — Phase 2 Complete

All Phase 1 and Phase 2 IPC components are implemented.  The subsystem now
supports blocking receives with timeout, port right transfer in messages,
out-of-line memory descriptors, no-senders notifications, message queue drain
on port destroy, and an IPC-based bootstrap service registry.

### Source Files

| File | Status | Description |
|------|--------|-------------|
| `ipc.h` / `ipc.c` | ✅ Done | Subsystem init; port alloc/destroy (drains mqueue); space create/destroy/lookup |
| `ipc_port.h` | ✅ Done | `struct ipc_port` — added `ip_nsrequest` for no-senders notification |
| `ipc_space.h` | ✅ Done | `struct ipc_space` — per-task port name table (fixed-size flat array) |
| `ipc_entry.h` | ✅ Done | `struct ipc_entry` — one slot in the name table; right bits and urefs encoding |
| `ipc_right.h` / `ipc_right.c` | ✅ Done | Added `ipc_right_nsnotify`, `ipc_right_request_notification`; no-senders delivered on last send right drop |
| `ipc_mqueue.h` / `ipc_mqueue.c` | ✅ Done | Extended `ipc_kmsg` with port/OOL slots; added `ipc_mqueue_dequeue`, `ipc_mqueue_dequeue_timeout`, `ipc_mqueue_drain`, `ipc_mqueue_enqueue` |
| `ipc_kmsg.h` / `ipc_kmsg.c` | ✅ Done | Complex message send/receive: port right extraction/installation, OOL copy; added `mach_msg_receive_timeout` |
| `mach_msg.h` / `mach_msg.c` | ✅ Done | `mach_msg_trap` accepts `timeout` param; `MACH_RCV_TIMEOUT` honoured |

### Phase 2 Features

- **Blocking receive with timeout** — `ipc_mqueue_dequeue_timeout()` busy-waits
  up to `timeout_ms` milliseconds using TSC (x86-64) or system counter (AArch64).
  `mach_msg_trap()` passes timeout through when `MACH_RCV_TIMEOUT` is set.
- **Port rights in messages** — `MACH_MSG_TYPE_COPY_SEND`, `MOVE_SEND`,
  `MOVE_SEND_ONCE`, `MOVE_RECEIVE`, `MAKE_SEND`, `MAKE_SEND_ONCE` all supported.
  Rights extracted from sender's space at send time; installed in receiver's
  space at receive time.
- **OOL memory descriptors** — buffers physically copied into `kalloc`-allocated
  regions at send time; delivered to receiver as kernel pointers.
- **No-senders notification** — `ipc_right_request_notification()` registers a
  notification port; `MACH_NOTIFY_NO_SENDERS` delivered when `ip_send_rights` → 0.
- **Message queue drain on port destroy** — `ipc_port_destroy()` now calls
  `ipc_mqueue_drain()`, freeing all queued messages and their OOL buffers.
- **IPC-based bootstrap server** — `servers/bootstrap/bootstrap_ipc.c` implements
  `BOOTSTRAP_IPC_MSG_REGISTER` / `BOOTSTRAP_IPC_MSG_LOOKUP` using real Mach messages.

## Phase 2 TODO (complete ✅)

- [x] Blocking receive with `MACH_RCV_TIMEOUT` support
- [x] Out-of-line memory descriptors (`mach_msg_ool_descriptor_t`)
- [x] Port rights carried in messages (`MACH_MSG_TYPE_MOVE_SEND` etc.)
- [x] No-senders notification delivery
- [x] Drain message queue on `ipc_port_destroy`
- [x] `tests/ipc/ipc_ool_test.c` — OOL buffer send/receive test
- [x] `tests/ipc/ipc_port_transfer_test.c` — port right transfer test
- [x] `tests/ipc/ipc_timeout_test.c` — blocking receive with timeout test
- [x] IPC-based bootstrap server (`servers/bootstrap/bootstrap_ipc.c`)

## Phase 3 TODO (next)

- [ ] Preemptive scheduling — replace busy-wait timeout with scheduler sleep/wakeup
- [ ] Dynamic `ipc_space` table — replace fixed 256-slot array with a growing table
- [ ] VM-level OOL zero-copy — use `vm_map()` instead of `kmemcpy()` for large OOL
- [ ] Port zone allocator — replace `kalloc` per-port with a dedicated slab zone
- [ ] Sleep locks — replace spinlocks with proper mutex/read-write locks

## References

- Mach 3.0 Kernel Principles (CMU TR) — Accetta et al., 1986
- GNU Mach `ipc/` directory
- XNU `osfmk/ipc/`
- Liedtke, "On µ-Kernel Construction" (SOSP 1995) §3 — IPC performance
- `docs/ipc-design.md` — NEOMACH IPC design decisions
- `docs/rfcs/RFC-0001-ipc-message-format.md` — IPC message format RFC
