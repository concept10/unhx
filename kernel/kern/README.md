# kernel/kern/

Task, thread, scheduler, and host/processor abstractions.

## Key Abstractions

- **task** — unit of resource ownership (address space, port namespace, threads)
- **thread** — unit of execution within a task
- **host** / **processor** — hardware abstraction
- **clock** — timekeeping and timer services

## Current Status — Phase 1 Complete

| File | Status | Description |
|------|--------|-------------|
| `task.h` / `task.c` | ✅ Done | Task create, destroy, reference counting, kernel task |
| `thread.h` / `thread.c` | ✅ Done | Thread create, context save/restore, lifecycle |
| `sched.h` / `sched.c` | ✅ Done | Cooperative round-robin scheduler; PIT timer setup |
| `kalloc.h` / `kalloc.c` | ✅ Done | Kernel heap — 256 KB bump allocator |
| `klib.h` / `klib.c` | ✅ Done | Minimal string/memory helpers (no libc dependency) |
| `kern.h` / `kern.c` | ✅ Done | Kernel entry point (`kernel_main`), subsystem init |
| `kernel_task.h` / `kernel_task.c` | ✅ Done | Kernel task (task 0) creation and IPC smoke test |
| `host.c` | 🔲 Phase 2 | Host object and processor management |
| `exception.c` | 🔲 Phase 2 | Mach exception ports and delivery |

The bootstrap server (`servers/bootstrap/bootstrap.c`) provides initial service
registration and port lookup for milestones v0.3+.

## Phase 2 TODO

- [ ] Preemptive scheduling — timer interrupt (IDT + APIC required)
- [ ] `host.c` — host object and processor management
- [ ] `exception.c` — Mach exception ports and delivery
- [ ] Sleep locks (replace spinlocks once scheduler can block/unblock)
- [ ] SMP-safe task/thread operations

## References

- GNU Mach `kern/`
- XNU `osfmk/kern/`
- Mach 3.0 Kernel Principles — Chapter 3: Tasks and Threads
