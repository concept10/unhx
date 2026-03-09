# servers/bootstrap/

Bootstrap server — initial service registry for NEOMACH.

## Responsibility

The bootstrap server is the first server task in Mach. It maintains a
name → port mapping so that clients can look up well-known services without
hard-coding port names.

In the full Mach design, the bootstrap server runs as a **separate userspace
task**, receives registration and lookup requests as Mach messages on the
bootstrap port, and replies with port send rights.

## Current Status — Phase 1 Complete ✅

Phase 1 implements the bootstrap server as a **kernel-internal function** that
demonstrates the registration and lookup flow. All three milestone checks pass:

- `bootstrap_register("com.neomach.kernel", port)` — succeeds
- `bootstrap_lookup("com.neomach.kernel")` — returns correct port
- `bootstrap_lookup("com.neomach.nonexistent")` — returns NOT_FOUND
- Duplicate registration is rejected

## Source Files

| File | Description |
|------|-------------|
| `bootstrap.h` / `bootstrap.c` | Bootstrap server entry point (`bootstrap_main`), `bootstrap_register`, `bootstrap_lookup` API |
| `registry.c` | Name → port table (simple fixed-size array, Phase 1) |

## Phase 2 TODO

Make the bootstrap server a real userspace task:

- [ ] Bootstrap server runs as a separate Mach task with its own address space
- [ ] Holds receive right to the bootstrap port
- [ ] Sits in a `mach_msg()` loop handling `bootstrap_register` and
      `bootstrap_lookup` message IDs
- [ ] Replies with port send rights to the requesting client
- [ ] Loaded by the kernel as the first user task from Multiboot module / initrd
- [ ] Replace fixed-size registry table with a dynamic hash map

## References

- CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap
- OSF MK `bootstrap/bootstrap.c` — original implementation
- GNU Mach `bootstrap/` — reference bootstrap server
- Mach 3.0 Server Writer's Guide — §3: Bootstrap Services
