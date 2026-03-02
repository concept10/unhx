# IPC Subsystem Design

Design decisions and rationale for the UNHOX Mach IPC implementation.

## Overview

UNHOX IPC follows the CMU Mach 3.0 model exactly: all inter-process
communication uses **port-based message passing**. Ports are the sole
mechanism for cross-domain interaction — there are no Unix-style pipes,
sockets, or shared memory primitives at the kernel level.

Reference: Accetta et al., "Mach: A New Kernel Foundation for UNIX Development"
(1986), §3.

## Core Abstractions

### Ports (`ipc_port`)

A port is a kernel-managed, unidirectional message queue. It has:

- **One receive right** — held by exactly one task (the "receiver"). Only this
  task can dequeue messages from the port.
- **Zero or more send rights** — distributed to other tasks. Any holder of a
  send right can enqueue messages on the port.
- **A message queue** (`ipc_mqueue`) — FIFO buffer of pending messages.

Ports are the capability tokens of Mach: possessing a send right IS the
authorization to communicate with a service. There is no separate ACL check.

### Port Name Spaces (`ipc_space`)

Each task has a private name space mapping integer names (`mach_port_name_t`)
to port rights. Port names are local to a task — name 5 in task A may refer to
a completely different port than name 5 in task B.

Phase 1 implementation: flat array of 256 entries. Dynamic hash table deferred
to Phase 2.

### Port Rights (`ipc_entry`)

An entry in the name space table. Each entry records:
- `ie_object` — pointer to the kernel `ipc_port`
- `ie_bits` — bitmask of rights held (SEND, SEND_ONCE, RECEIVE)

Right types:
- **SEND**: can send messages; reference-counted on the port
- **SEND_ONCE**: can send exactly one message; consumed on use
- **RECEIVE**: can receive messages; exclusive — exactly one holder

### Messages (`ipc_kmsg`)

A message is a header (`mach_msg_header_t`) followed by inline data. The header
contains:
- `msgh_remote_port` — destination port name (in sender's space)
- `msgh_local_port` — reply port name (in sender's space)
- `msgh_size` — total message size
- `msgh_id` — operation identifier (used by servers to dispatch)

Phase 1 uses copy semantics: the kernel copies the message into a kernel buffer
(`ipc_kmsg`) on send, and copies it out to the receiver's buffer on receive.

## Design Decisions

### 1. Copy Semantics (Phase 1)

**Decision**: Messages are copied into kernel buffers on send and copied out on
receive.

**Rationale**: Simplicity. Copy semantics avoid the complexity of VM-based
message passing (COW page remapping, out-of-line memory descriptors) while
being correct and sufficient for Phase 1 message sizes (≤1024 bytes).

**Future**: Phase 2 will add out-of-line memory descriptors for large transfers
(the kernel maps pages from sender to receiver using COW, avoiding copies).

### 2. Non-Blocking IPC (Phase 1)

**Decision**: Send and receive are non-blocking. Send fails if the queue is
full. Receive fails if the queue is empty.

**Rationale**: Blocking IPC requires thread sleep/wakeup integration with the
scheduler, which is not yet implemented. Non-blocking is sufficient for the
Phase 1 kernel-internal smoke test.

**Future**: Phase 2 will add blocking semantics:
- Send blocks if queue is full (configurable per-port queue limit)
- Receive blocks if queue is empty (thread sleeps until message arrives)
- Combined send+receive (RPC pattern) for handoff scheduling

### 3. Flat Name Space Table

**Decision**: `ipc_space` uses a fixed-size array of 256 entries.

**Rationale**: Avoids dynamic memory allocation complexity in Phase 1. 256
entries is sufficient for kernel-internal testing.

**Future**: Phase 2 will use a dynamically-resizable hash table (following
OSF MK's `ipc_splay_tree` or XNU's hash table approach).

### 4. Fixed Message Size Limit

**Decision**: Maximum inline message size is 1024 bytes
(`IPC_MQUEUE_MAX_MSG_SIZE`).

**Rationale**: Simplifies buffer management. All kernel-internal messages in
Phase 1 are small (< 256 bytes).

**Future**: Out-of-line descriptors will remove this limit for large transfers.

### 5. Spinlock-Based Synchronization

**Decision**: Port and queue locks use `atomic_flag` spinlocks.

**Rationale**: Phase 1 runs cooperatively with no preemption, so spinlocks
are safe and simple. No deadlock risk since there is no thread preemption.

**Future**: Phase 2 (with preemptive scheduling) will need to disable
interrupts while holding spinlocks, or transition to mutex + sleep/wakeup.

### 6. Capability Check at Send Time

**Decision**: The kernel checks for a SEND right in the sender's `ipc_space`
at `mach_msg_send()` time. If the right is not found, the send fails.

**Rationale**: This is the Mach security model. Port names are unforgeable —
a task cannot construct a port name that resolves to a port it doesn't hold
a right to.

## File Layout

```
kernel/ipc/
├── ipc.h           # Subsystem umbrella header
├── ipc.c           # ipc_port and ipc_space operations
├── ipc_port.h      # Port structure definition
├── ipc_space.h     # Per-task name space
├── ipc_entry.h     # Name space entry (right record)
├── ipc_mqueue.h    # Message queue structure and API
├── ipc_mqueue.c    # Queue enqueue/dequeue implementation
├── ipc_kmsg.h      # mach_msg_send / mach_msg_receive API
└── ipc_kmsg.c      # Kernel message send/receive implementation
```

## Message Flow

```
Sender Task                    Kernel                         Receiver Task
     │                           │                                  │
     │  mach_msg_send(msg)       │                                  │
     │──────────────────────────>│                                  │
     │                           │ 1. Lookup dest port in           │
     │                           │    sender's ipc_space            │
     │                           │ 2. Verify SEND right             │
     │                           │ 3. Check port alive              │
     │                           │ 4. Copy msg → ipc_kmsg           │
     │                           │ 5. Enqueue on port's mqueue      │
     │                           │                                  │
     │                           │         mach_msg_receive(port)   │
     │                           │<─────────────────────────────────│
     │                           │ 1. Lookup port in receiver's     │
     │                           │    ipc_space                     │
     │                           │ 2. Verify RECEIVE right          │
     │                           │ 3. Dequeue ipc_kmsg from mqueue  │
     │                           │ 4. Copy ipc_kmsg → receiver buf  │
     │                           │ 5. Free ipc_kmsg                 │
     │                           │──────────────────────────────────>│
```

## Phase 2 Roadmap

1. **Blocking IPC**: Thread sleep on empty receive, wakeup on send
2. **Combined send+receive**: Single syscall RPC pattern
3. **Out-of-line memory**: COW page transfer for large messages
4. **Port right transfer**: Pass port rights inside messages
5. **Dead-name notifications**: Notify senders when a port dies
6. **Port sets**: Receive from multiple ports simultaneously
7. **Dynamic name space**: Hash table replacing flat array
