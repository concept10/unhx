# NEOMACH Task List

Actionable tasks for the NEOMACH project, organized by phase.
Check off items as they are completed.

---

## Phase 0 — Source Archaeology & Repository Setup

### Repository Structure ✅
- [x] Create `kernel/` with `ipc/`, `vm/`, `kern/`, `platform/` subdirectories
- [x] Create `servers/` with `bsd/`, `vfs/`, `device/`, `network/`, `auth/` subdirectories
- [x] Create `frameworks/` with `DisplayServer/` directory
- [x] Create `archive/` with `cmu-mach/`, `osf-mk/`, `utah-oskit/`, `next-docs/` subdirectories
- [x] Create `docs/`, `tools/`, `tests/` directories
- [x] Write README files for all directories

### Git Submodules — Framework Layer ✅
- [x] Add `gnustep/libobjc2` as submodule at `frameworks/objc-runtime/`
- [x] Add `gnustep/libs-base` as submodule at `frameworks/Foundation/`
- [x] Add `gnustep/libs-gui` as submodule at `frameworks/AppKit/`
- [x] Add `apple/swift-corelibs-libdispatch` as submodule at `frameworks/libdispatch/`
- [x] Add `apple/swift-corelibs-foundation` as submodule at `frameworks/CoreFoundation/`

### Source Archaeology — Kernel References
- [ ] Mirror / obtain CMU Mach 3.0 source tree → `archive/cmu-mach/`
  - Source: http://www.cs.utah.edu/flux/mach4/ or bitsavers
  - Key files: `kernel/`, `include/mach/`, `bootstrap/`
  - Run: `./tools/mirror-archives.sh --cmu`
- [ ] Mirror / obtain OSF MK6 or MK7 source → `archive/osf-mk/`
  - Source: MkLinux archives, OSF/RI mirrors
  - Run: `./tools/mirror-archives.sh --osf`
- [ ] Mirror Utah OSKit + Lites → `archive/utah-oskit/`
  - Source: http://www.cs.utah.edu/flux/oskit/ and flux/lites
  - Run: `./tools/mirror-archives.sh --utah`
- [ ] Archive NeXTSTEP/OPENSTEP documentation → `archive/next-docs/`
  - Source: https://bitsavers.org/pdf/next/ and archive.org
  - Run: `./tools/mirror-archives.sh --next`

### Build System ✅
- [x] Create `CMakeLists.txt` at root for cross-compilation setup
- [x] Create `tools/cross/` with cross-compiler toolchain instructions
  - Target: `x86_64-unknown-elf` via Clang/LLD (see `cmake/x86_64-elf-clang.cmake`)
- [x] Create `tools/qemu-run.sh` script for QEMU boot testing
- [x] Create Nix flake (`flake.nix`) for reproducible dev environment
- [x] Document build prerequisites in `docs/build-setup.md`

### Documentation ✅
- [x] Write `docs/architecture.md` — system architecture overview
- [x] Write `docs/roadmap.md` — development roadmap with milestones
- [x] Write `docs/sources.md` — complete source inventory with licenses
- [x] Write `docs/ipc-design.md` — IPC subsystem design decisions
- [x] Write `docs/bsd-server-design.md` — BSD server architecture and hard problems
- [x] Create `docs/rfcs/` directory and write first RFC (RFC-0001: IPC message format)
- [x] Write `docs/future.md` — future core systems strategy (audio/MIDI/graphics/media/networking/security, L4/seL4 improvements, licensing matrix)

---

## Phase 1 — Kernel Core ✅ (2026-03-01)

### x86-64 Platform Bring-up ✅
- [x] Write `kernel/platform/x86_64/boot.S` — Multiboot1 entry point (32-bit PM → 64-bit long mode)
- [x] Write `kernel/platform/gdt.c` — Global Descriptor Table setup
- [x] Write `kernel/platform/paging.c` — 4-level page table management (identity map + higher-half)
- [x] Write `kernel/platform/platform.c` — NS16550 serial UART driver (COM1, 115200 baud)
- [x] Write `kernel/platform/context_switch.S` — Cooperative context switch (callee-saved GPRs)
- [x] Verified: kernel boots under QEMU and prints "NEOMACH" via serial (2026-03-01)
- [ ] Write `tests/integration/boot/boot_test.sh` — QEMU boot smoke test (automated CI)
- [ ] IDT setup and exception/interrupt handlers
- [ ] APIC initialization (local APIC, IOAPIC)

### AArch64 Platform Bring-up ✅ (partial)
- [x] Write `kernel/platform/aarch64/boot.S` — QEMU virt boot entry (EL2→EL1)
- [x] Write `kernel/platform/aarch64/platform.c` — PL011 UART driver
- [x] Write `kernel/platform/aarch64/context_switch.S` — AArch64 cooperative context switch
- [ ] MMU initialization (4-level translation tables)
- [ ] GIC interrupt controller
- [ ] SMP trampoline for additional CPUs

### Physical Memory ✅
- [x] Write `kernel/vm/vm_page.c` — physical page frame allocator
- [x] Parse Multiboot memory map to initialize page allocator
- [ ] Write unit test: allocate and free physical pages

### Kernel Heap ✅
- [x] Write `kernel/kern/kalloc.c` — kernel heap allocator (256 KB bump allocator)
- [ ] Write unit test: allocate and free kernel objects

### Tasks and Threads ✅
- [x] Write `kernel/kern/task.c` — task create, terminate, reference counting
- [x] Write `kernel/kern/thread.c` — thread create, context save/restore
- [x] Write `kernel/platform/context_switch.S` — context switch assembly
- [x] Write `kernel/kern/sched.c` — cooperative round-robin scheduler (Phase 1)
- [x] Verified: two tasks created and run (cooperative scheduling)
- [ ] Preemptive scheduling — timer interrupt required (Phase 2)
- [ ] Verify: two threads run concurrently with preemption (Phase 2)

### Virtual Memory ✅ (partial)
- [x] Write `kernel/vm/vm_page.c` — physical page frame allocator
- [x] Write `kernel/vm/vm_map.h` — per-task address space interface (Phase 1 stubs)
- [ ] Write `kernel/vm/vm_map.c` — full address space management (Phase 2)
- [ ] Write `kernel/vm/vm_object.c` — backing memory object lifecycle
- [ ] Write `kernel/vm/vm_fault.c` — page fault handler
- [ ] Verify: user task runs in its own address space (Phase 2)

### IPC ✅
- [x] Write `kernel/ipc/ipc_port.c` / `ipc.c` — Mach port creation and lifecycle
- [x] Write `kernel/ipc/ipc_space.c` / `ipc.c` — per-task port name space
- [x] Write `kernel/ipc/ipc_right.c` — port right management
- [x] Write `kernel/ipc/ipc_mqueue.c` — message queue (non-blocking, Phase 1)
- [x] Write `kernel/ipc/ipc_kmsg.c` — kernel message allocation
- [x] Write `kernel/ipc/mach_msg.c` — `mach_msg()` trap
- [x] Write `tests/ipc/ipc_roundtrip_test.c` — two-task message-passing test
- [x] Verified milestone v0.2: two tasks pass a Mach message — 13/13 tests PASS ✅

### Bootstrap Server ✅
- [x] Write `servers/bootstrap/bootstrap.c` — service registration and port lookup
- [x] Verified milestone v0.3: bootstrap server registers and looks up services ✅

### IPC Performance Baseline ✅
- [x] Write `tests/ipc/ipc_perf.c` — null Mach message round-trip benchmark (TSC-based)
- [x] Run benchmark under QEMU
- [ ] Run benchmark on bare metal (when available) and record
- [ ] Document results in `docs/research/ipc-performance.md`

---

## Phase 2 — System Servers

### BSD Server
- [x] Design BSD server IPC protocol (MIG or hand-written)
- [x] Implement `fork()` — clone task + address space
- [x] Implement `exec()` — load ELF binary into new address space
- [x] Implement `exit()` / `wait()` — process lifecycle
- [x] Implement signal delivery across task boundary
- [x] Implement file descriptor table (backed by VFS server)
- [ ] Verify milestone v0.4: fork + exec + basic syscalls work

### VFS Server (ramfs)
- [ ] Implement ramfs — in-memory directory tree
- [ ] Implement `open()`, `read()`, `write()`, `close()`, `stat()`
- [ ] Implement `readdir()`, `mkdir()`, `unlink()`
- [ ] Verify: BSD server can open and read `/bin/sh` from ramfs

### Shell
- [ ] Port `dash` (Debian Almquist Shell) to NEOMACH
- [ ] Verify milestone v0.5: shell prompt appears

---

## Phase 3 — Driver Layer

### QEMU Virtio Drivers (development priority)
- [ ] Implement virtio-blk block device driver
- [ ] Implement virtio-net network device driver
- [ ] Verify: read/write to virtio-blk disk image

### Storage & Filesystem
- [ ] Implement AHCI SATA driver in device server
- [ ] Implement NVMe driver in device server
- [ ] Implement ext2 filesystem translator in VFS server
- [ ] Verify milestone v0.6: boot from disk, read/write files

### Input & Display
- [ ] Implement USB HID keyboard driver
- [ ] Implement VESA/GOP framebuffer driver
- [ ] Implement PCI enumeration

---

## Phase 4 — Framework Layer

### Objective-C Runtime
- [ ] Build `frameworks/objc-runtime/` (libobjc2 submodule) for NEOMACH userspace
- [ ] Resolve any POSIX threading dependencies
- [ ] Verify: simple Objective-C program compiles and runs on NEOMACH

### GNUstep Foundation
- [ ] Build `frameworks/Foundation/` (libs-base submodule) for NEOMACH
- [ ] Port NSThread to use Mach thread primitives directly
- [ ] Port NSRunLoop to use Mach port notification
- [ ] Verify milestone v0.7: GNUstep Foundation app runs on NEOMACH

### libdispatch (GCD)
- [ ] Build libdispatch with Mach port integration
- [ ] Verify: `dispatch_async` works on NEOMACH

---

## Phase 5 — Desktop

### IPC Gate Conditions for Display Server (Phase A)
- [ ] Implement OOL memory descriptor support (`kernel/ipc/ipc_kmsg.c`, `kernel/vm/vm_map.c`)
- [ ] Implement port right transfer in messages (`kernel/ipc/ipc_kmsg.c`, `kernel/ipc/ipc_right.c`)
- [ ] Implement blocking receive with timeout (`kernel/ipc/ipc_mqueue.c`, `kernel/kern/thread.c`)
- [ ] Implement bootstrap server port lookup (`servers/bootstrap/`)
- [ ] Write `tests/ipc/ipc_ool_test.c` — OOL buffer send/receive test
- [ ] Write `tests/ipc/ipc_port_transfer_test.c` — port right transfer test
- [ ] Write `tests/ipc/ipc_timeout_test.c` — receive-with-timeout test

### Core Display Server (Phase B)
- [ ] Create `servers/display/core/` — `unhx-display` main loop
- [ ] Implement surface management (OOL buffer allocation, window list)
- [ ] Implement software compositor (CPU blit of client surfaces to framebuffer)
- [ ] Implement framebuffer output via device server
- [ ] Implement `libdisplay.a` client library (`display_connect`, `surface_create`, `surface_commit`)
- [ ] Write minimal AppKit test application using `libdisplay.a`
- [ ] Verify milestone v0.8: a window appears on screen

### Compatibility Personalities (Phase C)
- [ ] Implement X11 personality server (`servers/display/x11/`)
- [ ] Test X11 client (`xterm`) via X11 personality
- [ ] Implement Wayland personality server (`servers/display/wayland/`)
- [ ] Test Wayland client (`weston-terminal`) via Wayland personality

### Full Desktop (Phase D)
- [ ] Build AppKit (libs-gui) with NEOMACH display server backend
- [ ] Port GWorkspace as Workspace Manager
- [ ] GPU-accelerated compositor path in `unhx-display`
- [ ] Verify milestone v1.0: NeXT-heritage desktop boots

---

## Ongoing / Cross-Cutting

### Testing Infrastructure
- [ ] Set up CI: build kernel on push, run QEMU smoke test
- [ ] Kernel sanitizers: KASAN (address), KUBSAN (undefined behavior)
- [ ] Fuzz testing for IPC message parsing

### Documentation
- [ ] Document every design decision in `docs/rfcs/` before implementing
- [ ] Write historical context for each source in `archive/`
- [ ] Create contributor guide in `CONTRIBUTING.md`
- [ ] Write license inventory in `docs/sources.md`

### Kernel for QEMU Testing ✅
- [x] Set up dev environment — `nix develop` (see `tools/README.md`) or Homebrew LLVM
- [x] Write minimal Multiboot1 kernel stub — boots via QEMU and prints "NEOMACH" via serial
- [x] Create `tools/run-qemu.sh` and `tools/qemu-run.sh`:
  ```sh
  qemu-system-x86_64 \
    -kernel build/neomach.elf \
    -serial stdio \
    -display none \
    -m 64M \
    -no-reboot
  ```
- [x] Test kernel stub boots and produces expected serial output (2026-03-01)
- [ ] Pull GNU Mach source as local reference:
  ```sh
  git clone https://git.savannah.gnu.org/git/hurd/gnumach.git archive/gnu-mach-ref
  ```
- [ ] Pull HURD source as local reference:
  ```sh
  git clone https://git.savannah.gnu.org/git/hurd/hurd.git archive/hurd-ref
  ```
