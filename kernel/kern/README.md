# kernel/kern/

Task, thread, scheduler, and host/processor abstractions.

## Key Abstractions

- **task** — unit of resource ownership (address space, port namespace, threads)
- **thread** — unit of execution within a task
- **host** / **processor** — hardware abstraction
- **clock** — timekeeping and timer services

## Implementation Plan

- [ ] `task.h` / `task.c` — task create, terminate, reference counting
- [ ] `thread.h` / `thread.c` — thread create, switch, terminate
- [ ] `sched.c` — basic round-robin scheduler (priority later)
- [ ] `host.c` — host object and processor management
- [ ] `startup.c` — kernel startup sequence
- [ ] `bootstrap.c` — bootstrap server: initial task + port registration
- [ ] `exception.c` — Mach exception ports and delivery

## References

- GNU Mach `kern/`
- XNU `osfmk/kern/`
- Mach 3.0 Kernel Principles — Chapter 3: Tasks and Threads
