# NEOMACH Kernel

NEOMACH implements a true microkernel following the original Mach design from
Carnegie Mellon University (CMU, 1985–1990) and the Open Software Foundation
(OSF MK series).

## Mach Design Philosophy

### Kernel Minimality

The NEOMACH kernel implements **only** the following subsystems:

1. **IPC (Mach Ports)** — the sole communication mechanism between all
   components, both in-kernel and in userspace.
2. **Virtual Memory** — page-level memory management with support for
   external pagers (memory objects backed by userspace servers).
3. **Tasks and Threads** — tasks own resources (address space, port
   namespace); threads are units of execution within a task.
4. **Scheduling** — decides which thread runs next.

Everything else — BSD personality, filesystems, device drivers, networking —
lives in **userspace servers** that communicate with each other and with the
kernel exclusively through Mach IPC.  This is the fundamental distinction
between NEOMACH and monolithic kernels such as Linux, and between NEOMACH and
hybrid kernels such as XNU (macOS/iOS), which collapsed BSD back into the
kernel for performance.  We will not repeat that mistake without a measured
benchmark proving it is necessary.

### Ports as Capabilities

A Mach port is a protected message queue owned by the kernel.  A **port
right** is an unforgeable capability token:

- **RECEIVE right** — exclusive; one per port; allows dequeuing messages.
- **SEND right** — copyable; allows enqueuing messages.
- **SEND_ONCE right** — consumed after a single use.

Port names (small integers) live in a task's **port space** (`ipc_space`).
The kernel maps those names to kernel-internal port objects (`ipc_port`).
User tasks never see kernel pointers; this indirection is the foundation of
Mach's security model.

Holding a send right to a port **is** the permission to use that service.
No additional access-control checks are layered on top.

### Reference Documents

- Accetta et al., "Mach: A New Kernel Foundation for UNIX Development",
  USENIX 1986 — foundational design paper.
- OSF/RI MK series documentation — OSF Mach 3.0 implementation notes.
- Tevanian et al., "Mach Threads and the Unix Kernel: The Battle for
  Control", USENIX 1987 — thread and scheduler design.

## Directory Layout

```
kernel/
├── ipc/          Mach IPC: ports, port spaces, message queues
├── kern/         Tasks, threads, scheduler
├── vm/           Virtual memory: page allocator, vm_map, vm_object
├── platform/     x86-64 hardware abstraction: boot, GDT, paging, interrupts
├── include/
│   └── mach/     Public Mach type and constant headers
├── CMakeLists.txt
└── linker.ld     Higher-half kernel linker script
```

## Build

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake
cmake --build build
```

Run under QEMU:

```sh
tools/run-qemu.sh
```

Debug with GDB:

```sh
# Terminal 1 — already running: tools/run-qemu.sh
tools/debug-qemu.sh
```

## Current Status — Phase 1 Complete ✅

The kernel achieved its first successful boot on **2026-03-01**.

| Subsystem | Status | Notes |
|-----------|--------|-------|
| Platform / Boot (x86-64) | ✅ | Multiboot1, GDT, 4-level paging, NS16550 UART |
| Platform / Boot (AArch64) | ✅ | QEMU virt, PL011 UART |
| Context Switch | ✅ | Cooperative (callee-saved GPRs) |
| Kernel Heap | ✅ | 256 KB bump allocator (`kalloc`) |
| Physical Memory | ✅ | Page frame allocator (`vm_page`) |
| IPC | ✅ | Ports, spaces, queues, `mach_msg` — 13/13 milestone tests PASS |
| Tasks & Threads | ✅ | Task/thread lifecycle, cooperative round-robin scheduler |
| Bootstrap Server | ✅ | Name→port registry (`servers/bootstrap/`) |
| IDT / Interrupts | 🔲 Phase 2 | Required for preemptive scheduling |
| VM Maps (full) | 🔲 Phase 2 | Userspace address spaces |
| VM Objects / Fault | 🔲 Phase 2 | External pager protocol |
| Blocking IPC | 🔲 Phase 2 | Thread sleep/wakeup on port queues |
