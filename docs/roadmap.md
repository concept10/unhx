# UNHOX Development Roadmap

Full roadmap from the UNHOX Project Proposal.

## Milestones

| Milestone | Name | Success Criteria |
|-----------|------|-----------------|
| v0.1 | Mach Boots | Kernel boots under QEMU, serial output |
| v0.2 | IPC Works | Two tasks pass a Mach message |
| v0.3 | Bootstrap | Bootstrap server, service registration |
| v0.4 | BSD Server | Fork, exec, basic POSIX syscalls |
| v0.5 | Shell | A shell prompt running on UNHOX |
| v0.6 | Disk & FS | Boot from disk, read/write files |
| v0.7 | Foundation | GNUstep Foundation app runs |
| v0.8 | Display | Framebuffer + minimal window system |
| v1.0 | Desktop | Full NeXT-heritage desktop boots |

---

## Phase 0 — Source Archaeology & Repository Setup

**Deliverable:** Structured monorepo with all sources catalogued and buildable.

- [ ] Mirror CMU Mach 3.0 sources to `archive/cmu-mach/`
- [ ] Mirror OSF MK6/MK7 sources to `archive/osf-mk/`
- [ ] Mirror Utah OSKit/Lites to `archive/utah-oskit/`
- [ ] Archive NeXTSTEP/OPENSTEP documentation to `archive/next-docs/`
- [ ] Add GNUstep submodules (`frameworks/objc-runtime/`, `frameworks/Foundation/`, `frameworks/AppKit/`)
- [ ] Establish build system (CMake + Nix)
- [ ] Document all source licenses in `docs/sources.md`

## Phase 1 — Kernel Core (The Mach Minimum)

**Deliverable:** A kernel that boots, creates two tasks, and passes a message between them.

- [ ] x86-64 boot via Multiboot2 under QEMU
- [ ] Serial console output
- [ ] Physical memory allocator
- [ ] Task and thread creation
- [ ] Basic round-robin scheduler
- [ ] Mach port creation and IPC send/receive
- [ ] Virtual memory — basic maps and objects
- [ ] Bootstrap server — initial service registration
- [ ] QEMU test script: boot and print "UNHOX v0.1"
- [ ] Expand unit tests: `tests/unit/ipc/` for IPC invariants ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 1)

## Phase 2 — System Servers

**Deliverable:** A userspace shell running on UNHOX.

- [ ] BSD server — fork, exec, exit, wait
- [ ] Signal delivery across process boundary
- [ ] ramfs VFS server
- [ ] Basic device server (keyboard, framebuffer)
- [ ] Network server (lwIP integration)
- [ ] Port a minimal shell (e.g., `dash` or `pdksh`)
- [ ] Property-based tests for IPC/VM (Hypothesis) ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 2)
- [ ] TLA+ model of port capability system; TLC verification ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 3)
- [ ] SPIN model of `ipc_mqueue`; deadlock-freedom check ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 3)
- [ ] libFuzzer IPC harness with ASan/UBSan ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 3)

## Phase 3 — Driver Layer & Real Hardware

**Deliverable:** UNHOX boots from disk, reads files, handles input.

- [ ] PCI enumeration in device server
- [ ] AHCI/NVMe storage driver
- [ ] virtio-blk and virtio-net drivers
- [ ] USB HID driver (keyboard/mouse)
- [ ] VESA/GOP framebuffer driver
- [ ] ext2 filesystem translator in VFS server
- [ ] Boot from disk image
- [ ] Isabelle/HOL abstract spec + refinement proof for IPC ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 4)

## Phase 4 — Framework Layer (NeXT Stack)

**Deliverable:** A GNUstep application running on UNHOX.

- [ ] Port libobjc2 to UNHOX userspace (no POSIX threading conflicts)
- [ ] Port GNUstep Foundation (libs-base) to UNHOX
- [ ] Port libdispatch with Mach port integration
- [ ] Minimal display server prototype
- [ ] AppKit backend for UNHOX display server
- [ ] VM memory-isolation proof in Isabelle/HOL ([RFC-0002](rfcs/RFC-0002-kernel-verification.md) Layer 4)

## Phase 5 — Desktop & Full Stack

**Deliverable:** A bootable UNHOX image with a complete NeXT-heritage desktop.

- [ ] Workspace Manager (GWorkspace-based)
- [ ] Display PostScript-inspired compositing server
- [ ] Interface Builder port
- [ ] Package management
- [ ] Self-hosting build (UNHOX builds UNHOX)
