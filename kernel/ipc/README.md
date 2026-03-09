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

## Current Status — Phase 1 Complete

All Phase 1 IPC components are implemented. The subsystem provides non-blocking
send and receive using copy semantics and spinlock-based synchronisation.

### Source Files

| File | Status | Description |
|------|--------|-------------|
| `ipc.h` / `ipc.c` | ✅ Done | Subsystem init (`ipc_init`); port alloc/destroy; space create/destroy/lookup |
| `ipc_port.h` | ✅ Done | `struct ipc_port` — kernel-internal port object, type flags, spinlock helpers |
| `ipc_space.h` | ✅ Done | `struct ipc_space` — per-task port name table (fixed-size flat array, Phase 1) |
| `ipc_entry.h` | ✅ Done | `struct ipc_entry` — one slot in the name table; right bits and urefs encoding |
| `ipc_right.h` / `ipc_right.c` | ✅ Done | `ipc_right_alloc_receive`, `copy_send`, `make_send_once`, `deallocate`, `transfer` |
| `ipc_mqueue.h` / `ipc_mqueue.c` | ✅ Done | Per-port FIFO message queue; non-blocking send/receive; copy semantics |
| `ipc_kmsg.h` / `ipc_kmsg.c` | ✅ Done | `mach_msg_send` / `mach_msg_receive` — capability-checked kernel IPC entry points |
| `mach_msg.h` / `mach_msg.c` | ✅ Done | `mach_msg_trap` (SEND \| RCV combined); `mach_msg_rpc` RPC helper |

### Phase 1 Constraints

- **Non-blocking only** — send returns `MACH_SEND_NO_BUFFER` if the queue is full;
  receive returns `KERN_FAILURE` if no message is available. Thread sleep/wakeup
  requires the scheduler and is deferred to Phase 2.
- **Fixed-size port space** — each task's `ipc_space` is a flat array of
  `IPC_SPACE_MAX_ENTRIES` (256) slots. A dynamic, growing table is a Phase 2 item.
- **Inline messages only** — maximum message size is `IPC_MQUEUE_MAX_MSG_SIZE`
  (1024 bytes). Out-of-line memory descriptors are a Phase 2 item.
- **Spinlocks** — `ip_lock` and `is_lock` are spinlocks (`atomic_flag`). These
  will be replaced with sleep locks once the Phase 2 scheduler is available.
- **No port-rights-in-messages** — typed descriptors carrying SEND/RECEIVE rights
  across task boundaries via message are not yet implemented.

## Phase 2 TODO

- [ ] Blocking send: block sender thread when queue is full; wake on dequeue
- [ ] Blocking receive: block receiver thread when queue is empty; wake on enqueue
- [ ] `MACH_RCV_TIMEOUT` — timer-based wakeup for timed receives
- [ ] Dynamic `ipc_space` table — replace fixed 256-slot array with a growing hash table
- [ ] Out-of-line memory descriptors — map VM regions into receiver's address space
- [ ] Port rights carried in messages — `MACH_MSG_TYPE_MOVE_SEND` etc.
- [ ] No-senders notification — deliver notification to receiver when `ip_send_rights` drops to zero
- [ ] Drain message queue and notify waiters on `ipc_port_destroy`
- [ ] Replace spinlocks with sleep locks (read/write where appropriate)
- [ ] Port zone allocator — replace `kalloc` per-port with a dedicated slab zone
- [ ] `tests/ipc/ipc_roundtrip_test.c` — two-task message-passing integration test
- [ ] `tests/ipc/ipc_perf.c` — null Mach message round-trip benchmark

## References

- Mach 3.0 Kernel Principles (CMU TR) — Accetta et al., 1986
- GNU Mach `ipc/` directory
- XNU `osfmk/ipc/`
- Liedtke, "On µ-Kernel Construction" (SOSP 1995) §3 — IPC performance
- `docs/ipc-design.md` — NEOMACH IPC design decisions
- `docs/rfcs/RFC-0001-ipc-message-format.md` — IPC message format RFC
