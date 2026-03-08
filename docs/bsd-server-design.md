# BSD Server Architecture

Design document for the NEOMACH BSD personality server — the component that
provides POSIX/Unix system call semantics on top of the Mach microkernel.

## Overview

In a Mach microkernel system, the kernel provides only IPC, VM, tasks, and
threads. All Unix semantics — processes, signals, file descriptors, fork/exec —
are implemented by a **userspace server** called the BSD server.

NEOMACH follows the same split as CMU Mach 3.0 + Lites and NeXTSTEP/XNU:

```
  User Process
       │
       │ (Unix syscall: fork, read, write, ...)
       │
       ▼
  BSD Server              (userspace, talks Mach IPC)
       │
       ├── VFS Server     (file operations → filesystem translators)
       ├── Net Server     (socket operations → network stack)
       └── Auth Server    (credential checks)
       │
       ▼
  Mach Kernel             (IPC, VM, tasks, threads only)
```

## Architecture

### IPC Protocol

Every Unix system call is translated into a Mach IPC message to the BSD server:

1. User process calls `read(fd, buf, count)`
2. C library traps into the kernel with `mach_msg()` (send + receive)
3. Kernel delivers the message to the BSD server's service port
4. BSD server processes the request, interacts with VFS/device servers as needed
5. BSD server sends a reply message back to the caller
6. C library returns the result to the user process

The message format will follow MIG (Mach Interface Generator) conventions
or a hand-written equivalent:

```c
struct bsd_read_request {
    mach_msg_header_t  header;    /* standard Mach header */
    int32_t            bsd_op;    /* BSD_READ */
    int32_t            fd;
    uint32_t           count;
};

struct bsd_read_reply {
    mach_msg_header_t  header;
    kern_return_t      retval;    /* 0 = success, or errno */
    uint32_t           bytes_read;
    uint8_t            data[];    /* inline for small reads */
};
```

### Process Model

The BSD server maps Unix processes onto Mach tasks:

| Unix Concept | Mach Equivalent | BSD Server Role |
|-------------|-----------------|-----------------|
| Process (PID) | Task | Maintains PID table, tracks parent/child |
| Thread | Thread | Manages thread groups, TLS |
| fork() | task_create() + vm_copy() | Clones address space via Mach VM |
| exec() | vm_deallocate() + load ELF | Tears down old space, loads new binary |
| exit() | task_terminate() | Notifies parent, collects status |
| wait() | port notification | Listens on child death port |
| Signal | IPC notification | Delivers signal via Mach exception port |

### File Descriptor Table

The BSD server maintains a per-process file descriptor table entirely in
userspace (within the BSD server's address space). File descriptors map to
VFS server ports:

```
BSD Server State (per process):
  fd_table[0] = { vfs_port: <port to VFS for /dev/console>, offset: 0 }
  fd_table[1] = { vfs_port: <port to VFS for /dev/console>, offset: 0 }
  fd_table[2] = { vfs_port: <port to VFS for /dev/console>, offset: 0 }
  fd_table[3] = { vfs_port: <port to VFS for /tmp/foo.txt>, offset: 128 }
```

### Signal Delivery

Signals are implemented via Mach exception ports:

1. BSD server allocates a signal port for each process
2. When a signal is sent (e.g., `kill(pid, SIGTERM)`), the BSD server sends a
   Mach message to the target process's signal port
3. A signal handler thread in the target process receives the message and
   dispatches the signal handler (or terminates)

## Hard Problems

### 1. fork() Performance

`fork()` requires cloning the entire address space. In Mach, this means:
- Creating a new task with `task_create()`
- Copying all VM mappings with COW (copy-on-write) semantics
- Duplicating the BSD server's per-process state (fd table, signal masks, etc.)

**Approach**: Use Mach's inherent COW support in `vm_map_copy()`. The actual
page data is shared until either process writes, at which point the page fault
handler creates a private copy.

### 2. exec() Atomicity

`exec()` must atomically replace a process's image:
- Tear down the old address space
- Load the new ELF binary
- Set up the new stack (argv, envp, auxv)
- Reset signal dispositions

If the ELF load fails, the process should be killed (not left in a broken
state).

### 3. Signal Races

Signals can arrive at any time, including during system calls. The BSD server
must handle:
- Interrupted system calls (EINTR)
- SA_RESTART semantics
- Signal mask management (sigprocmask)
- Reentrant signal handlers

### 4. Multi-Threaded fork()

`fork()` in a multi-threaded process only copies the calling thread. This
creates complex state where mutexes may be locked by threads that no longer
exist in the child. POSIX requires `pthread_atfork()` handlers.

### 5. IPC Overhead

Every system call is a Mach IPC round-trip (two context switches minimum).
Mitigation strategies:
- Combined send+receive RPC pattern (one trap instead of two)
- Handoff scheduling (sender directly switches to server thread)
- Batched operations for sequences of small calls
- Short-circuit paths for common operations

## File Layout (Planned)

```
servers/bsd/
├── bsd_server.c      # Main event loop (receive messages, dispatch)
├── bsd_process.c     # Process table, fork, exec, exit, wait
├── bsd_signal.c      # Signal delivery and handling
├── bsd_fd.c          # File descriptor table management
├── bsd_mig.h         # MIG-style message definitions
└── Makefile          # Userspace build (links against Mach syscall stubs)
```

## Dependencies

- **Mach IPC** (Phase 1 — done): Port creation, message send/receive
- **Mach VM** (Phase 2): Address space cloning for fork(), ELF loading
- **VFS Server**: File operations (open, read, write, close)
- **Device Server**: Device file operations (/dev/*)
- **Auth Server**: Credential validation (uid/gid checks)

## Phase 2 Implementation Order

1. Minimal BSD server with `write()` to serial (for "hello world")
2. `fork()` — clone task + address space
3. `exec()` — ELF loader
4. `exit()` / `wait()` — process lifecycle
5. File descriptor table + VFS integration
6. Signal delivery
7. Full POSIX syscall surface (enough for `dash` shell)
