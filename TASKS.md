# UNHOX Task List

Actionable tasks for the UNHOX project, organized by phase.
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

---

## Phase 1 — Kernel Core

### x86-64 Platform Bring-up
- [ ] Write `kernel/platform/x86_64/boot.S` — Multiboot2 entry point
- [ ] Write `kernel/platform/x86_64/gdt.c` — Global Descriptor Table setup
- [ ] Write `kernel/platform/x86_64/idt.c` — Interrupt Descriptor Table
- [ ] Write `kernel/platform/x86_64/pmap.c` — 4-level page table management
- [ ] Write `kernel/platform/x86_64/uart.c` — 16550 serial console driver
- [ ] Verify: kernel boots under QEMU and prints "UNHOX" via serial
- [ ] Write `tests/integration/boot/boot_test.sh` — QEMU boot smoke test

### Physical Memory
- [ ] Write `kernel/vm/vm_page.c` — physical page frame allocator
- [ ] Parse Multiboot memory map to initialize page allocator
- [ ] Write unit test: allocate and free physical pages

### Kernel Heap
- [ ] Write `kernel/kern/kmem.c` — kernel heap allocator (simple slab or buddy)
- [ ] Write unit test: allocate and free kernel objects

### Tasks and Threads
- [ ] Write `kernel/kern/task.c` — task create, terminate, reference counting
- [ ] Write `kernel/kern/thread.c` — thread create, context save/restore
- [ ] Write `kernel/platform/x86_64/context.S` — context switch assembly
- [ ] Write `kernel/kern/sched.c` — basic round-robin scheduler
- [ ] Verify: two threads run concurrently and print alternating output

### Virtual Memory
- [ ] Write `kernel/vm/vm_map.c` — per-task address space management
- [ ] Write `kernel/vm/vm_object.c` — backing memory object lifecycle
- [ ] Write `kernel/vm/vm_fault.c` — page fault handler
- [ ] Verify: user task runs in its own address space

### IPC
- [ ] Write `kernel/ipc/ipc_port.c` — Mach port creation and lifecycle
- [ ] Write `kernel/ipc/ipc_space.c` — per-task port name space
- [ ] Write `kernel/ipc/ipc_right.c` — port right management
- [ ] Write `kernel/ipc/ipc_mqueue.c` — message queue with blocking
- [ ] Write `kernel/ipc/ipc_kmsg.c` — kernel message allocation
- [ ] Write `kernel/ipc/mach_msg.c` — `mach_msg()` trap
- [ ] Write `tests/ipc/ipc_roundtrip_test.c` — two-task message-passing test
- [ ] Verify milestone v0.2: two tasks pass a Mach message

### Bootstrap Server
- [ ] Write `kernel/kern/bootstrap.c` — initial service registration
- [ ] Verify milestone v0.3: bootstrap server registers device and BSD servers

### IPC Performance Baseline
- [ ] Write `tests/ipc/ipc_perf.c` — null Mach message round-trip benchmark
- [ ] Run benchmark under QEMU and record baseline
- [ ] Run benchmark on bare metal (when available) and record
- [ ] Document results in `docs/research/ipc-performance.md`

---

## Phase 2 — System Servers

### BSD Server
- [ ] Design BSD server IPC protocol (MIG or hand-written)
- [ ] Implement `fork()` — clone task + address space
- [ ] Implement `exec()` — load ELF binary into new address space
- [ ] Implement `exit()` / `wait()` — process lifecycle
- [ ] Implement signal delivery across task boundary
- [ ] Implement file descriptor table (backed by VFS server)
- [ ] Verify milestone v0.4: fork + exec + basic syscalls work

### VFS Server (ramfs)
- [ ] Implement ramfs — in-memory directory tree
- [ ] Implement `open()`, `read()`, `write()`, `close()`, `stat()`
- [ ] Implement `readdir()`, `mkdir()`, `unlink()`
- [ ] Verify: BSD server can open and read `/bin/sh` from ramfs

### Shell
- [ ] Port `dash` (Debian Almquist Shell) to UNHOX
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
- [ ] Build `frameworks/objc-runtime/` (libobjc2 submodule) for UNHOX userspace
- [ ] Resolve any POSIX threading dependencies
- [ ] Verify: simple Objective-C program compiles and runs on UNHOX

### GNUstep Foundation
- [ ] Build `frameworks/Foundation/` (libs-base submodule) for UNHOX
- [ ] Port NSThread to use Mach thread primitives directly
- [ ] Port NSRunLoop to use Mach port notification
- [ ] Verify milestone v0.7: GNUstep Foundation app runs on UNHOX

### libdispatch (GCD)
- [ ] Build libdispatch with Mach port integration
- [ ] Verify: `dispatch_async` works on UNHOX

---

## Phase 5 — Desktop (Display Server v1 — Software Compositor)

Reference: `docs/rfcs/RFC-0002-display-server-architecture.md`

### Display Server IPC Protocol
- [x] Write `docs/display-server-architectures.md` — X11, Wayland, DPS, Quartz, Vulkan, DirectX survey
- [x] Write `docs/graphics-pipeline-microkernel.md` — GPU pipeline, shaders, ray tracing, AI/ML in microkernels
- [x] Write `docs/rfcs/RFC-0002-display-server-architecture.md` — UNHOX display server RFC
- [ ] Write `frameworks/DisplayServer/include/display_msg.h` — Mach IPC message definitions (RFC-0002 §IPC Protocol)
- [ ] Write `frameworks/DisplayServer/client/libdisplay.c` — client-side library (DS_CREATE_WINDOW etc.)
- [ ] Write `frameworks/DisplayServer/server/ds_main.c` — mach_msg receive dispatch loop
- [ ] Write `frameworks/DisplayServer/server/ds_window.c` — window registry (create, destroy, z-order)
- [ ] Write `frameworks/DisplayServer/server/ds_compositor.c` — Porter-Duff software compositor
- [ ] Write `frameworks/DisplayServer/server/ds_input.c` — keyboard/pointer event routing

### Input Device Server
- [ ] Write `servers/device/input/` — raw input event server (keyboard, pointer)
- [ ] Deliver input events to display server via Mach IPC

### AppKit Integration
- [ ] Write `frameworks/AppKit/backend/unhox/` — UNHOX display server backend for libs-gui
- [ ] Map NSWindow / NSView / NSEvent to display_msg.h protocol
- [ ] Port GWorkspace as Workspace Manager
- [ ] Verify milestone v1.0: NeXT-heritage desktop boots

---

## Phase 6 — GPU Acceleration

Reference: `docs/graphics-pipeline-microkernel.md` §8 (UNHOX GPU Server Design)

### GPU Device Server
- [ ] Write `servers/device/gpu/` — GPU device server (Mach IPC interface)
- [ ] Implement GPU VA space management (per-client GPU address space)
- [ ] Implement command buffer submission (amdgpu / virtio-gpu)
- [ ] Implement OOL GPU buffer sharing (Mach memory entry ↔ GPU VA)
- [ ] Implement async GPU fence delivery via Mach messages
- [ ] Verify: client renders Vulkan triangle; compositor composites it

### Vulkan Compositor
- [ ] Replace software compositor with Vulkan compute pass
- [ ] Implement `VK_KHR_display` scanout in display server
- [ ] Implement explicit sync (GPU fence → Mach message → compositor)
- [ ] Verify milestone v1.1: hardware-accelerated desktop at 60 fps

---

## Phase 7 — Ray Tracing and AI/ML Inference

Reference: `docs/graphics-pipeline-microkernel.md` §5 and §9

### Ray Tracing
- [ ] Expose `VK_KHR_ray_tracing_pipeline` via GPU device server
- [ ] BLAS/TLAS management API in device server
- [ ] Write `docs/rfcs/RFC-0003-gpu-inference-service.md`

### AI/ML Inference Service
- [ ] Write `servers/inference/` — ONNX Runtime inference server (Mach port)
- [ ] Integrate Vulkan EP (or llvmpipe fallback) in inference server
- [ ] Display server: optional AI upscaling pass (DLSS/FSR-style) in compositor
- [ ] Verify: inference server responds to Mach IPC requests with model outputs

---

## Ongoing / Cross-Cutting

### Testing Infrastructure
- [ ] Set up CI: build kernel on push, run QEMU smoke test
- [ ] Kernel sanitizers: KASAN (address), KUBSAN (undefined behavior)
- [ ] Fuzz testing for IPC message parsing

### Documentation
- [x] Write `docs/display-server-architectures.md` — comprehensive display server survey
- [x] Write `docs/graphics-pipeline-microkernel.md` — GPU pipeline in microkernel context
- [x] Write `docs/rfcs/RFC-0002-display-server-architecture.md` — display server RFC
- [ ] Document every design decision in `docs/rfcs/` before implementing
- [ ] Write historical context for each source in `archive/`
- [ ] Create contributor guide in `CONTRIBUTING.md`
- [ ] Write license inventory in `docs/sources.md`

### Kernel for QEMU Testing (near-term action)
- [ ] Set up dev environment — recommended: `nix develop` (see `tools/README.md`)
  - Alternative (Debian/Ubuntu): `sudo apt-get install qemu-system-x86_64 gcc-multilib grub-pc-bin grub-efi-amd64-bin xorriso`
  - Alternative (Fedora/RHEL): `sudo dnf install qemu-system-x86 grub2-tools xorriso`
  - Alternative (macOS/Homebrew): `brew install x86_64-elf-gcc qemu xorriso`
- [ ] Write minimal Multiboot2 kernel stub that prints "UNHOX" via serial
- [ ] Create `tools/qemu-run.sh`:
  ```sh
  qemu-system-x86_64 \
    -kernel kernel/build/unhox-kernel \
    -serial stdio \
    -display none \
    -m 256M \
    -no-reboot
  ```
- [ ] Test kernel stub boots and produces expected serial output
- [ ] Pull GNU Mach source as local reference:
  ```sh
  git clone https://git.savannah.gnu.org/git/hurd/gnumach.git archive/gnu-mach-ref
  ```
- [ ] Pull HURD source as local reference:
  ```sh
  git clone https://git.savannah.gnu.org/git/hurd/hurd.git archive/hurd-ref
  ```
