# NEOMACH — The Mach Kernel Reborn

> *The kernel Mach always wanted to become.*

NEOMACH is an open-source research and implementation project building a true
running microkernel operating system grounded in the original design philosophy
of the Mach kernel.

**This is not a clone of Linux. It is not a fork of XNU. It is not HURD.**
It draws from all of them and attempts a clean, principled synthesis: a modern,
bootable Mach-based OS with a full software stack including the NeXT/OpenStep
frameworks and desktop environment.

---

## Repository Structure

```
neomach/
├── kernel/           # NEOMACH Mach microkernel (new code)
│   ├── ipc/          # Mach port IPC implementation
│   ├── vm/           # Virtual memory subsystem
│   ├── kern/         # Task, thread, scheduler
│   └── platform/     # x86-64 / AArch64 HAL
├── servers/          # Userspace personality servers
│   ├── bsd/          # POSIX/BSD personality server
│   ├── vfs/          # Virtual filesystem server
│   ├── device/       # Device server + drivers
│   ├── network/      # Network stack server
│   └── auth/         # Auth/capability server
├── frameworks/       # NeXT/OpenStep framework layer
│   ├── objc-runtime/ # libobjc2 submodule (gnustep/libobjc2)
│   ├── Foundation/   # GNUstep libs-base submodule
│   ├── AppKit/       # GNUstep libs-gui submodule
│   └── DisplayServer/ # DPS-inspired compositor (new)
├── archive/          # Historical source references
│   ├── cmu-mach/     # CMU Mach 3.0 reference
│   ├── osf-mk/       # OSF MK6/MK7 reference
│   ├── utah-oskit/   # Utah OSKit + Lites
│   └── next-docs/    # NeXT documentation archive
├── docs/             # Design docs, RFCs, research notes
├── tools/            # Build tools, image creation
├── tests/            # Kernel test harness, QEMU scripts
├── TASKS.md          # Actionable task list with current status
└── README.md         # This file
```

## Quick Start

```sh
# Clone with all submodules
git clone --recurse-submodules https://github.com/concept10/neomach.git

# Or initialize submodules after cloning
git submodule update --init --recursive
```

## Current Status

**Phase 0 — Source Archaeology & Repository Setup** (in progress)

- [x] Repository directory structure created
- [x] Framework submodules added (libobjc2, GNUstep Foundation, AppKit)
- [ ] CMU Mach 3.0 sources mirrored to `archive/cmu-mach/`
- [ ] Build system (CMake + Nix)
- [ ] First kernel code

See [TASKS.md](TASKS.md) for the full actionable task list.

## Full Software Stack

```
┌─────────────────────────────────────────────────────────────┐
│            NEOMACH Full Software Stack                        │
├─────────────────────────────────────────────────────────────┤
│  Workspace Manager  │  AppKit  │  Display Server (DPS)      │
├─────────────────────┼──────────┼────────────────────────────┤
│  Foundation Kit  │  libobjc2  │  libdispatch  │  CoreFnd   │
├─────────────────────────────────────────────────────────────┤
│  BSD Server  │  VFS Server  │  Net Server  │  Auth Server  │
├─────────────────────────────────────────────────────────────┤
│  Device Server  │  Bootstrap Server  │  Pager Servers       │
├─────────────────────────────────────────────────────────────┤
│  MACH MICROKERNEL  (IPC · VM · Tasks · Threads · Sched)    │
├─────────────────────────────────────────────────────────────┤
│  Hardware Abstraction  (x86-64 / AArch64)                   │
└─────────────────────────────────────────────────────────────┘
```

## Milestones

| Milestone | Name | Success Criteria |
|-----------|------|-----------------|
| v0.1 | Mach Boots | Kernel boots under QEMU, serial output |
| v0.2 | IPC Works | Two tasks pass a Mach message |
| v0.3 | Bootstrap | Bootstrap server, service registration |
| v0.4 | BSD Server | Fork, exec, basic POSIX syscalls |
| v0.5 | Shell | A shell prompt running on NEOMACH |
| v0.6 | Disk & FS | Boot from disk, read/write files |
| v0.7 | Foundation | GNUstep Foundation app runs |
| v0.8 | Display | Framebuffer + minimal window system |
| v1.0 | Desktop | Full NeXT-heritage desktop boots |

## Design Principles

1. **Kernel Minimality** — If it can live outside the kernel, it does.
2. **Ports as Capabilities** — A port right is authority.
3. **Measure Before Compromising** — IPC performance is measured on real hardware before any architectural shortcuts.
4. **Clean Interfaces Over Premature Optimization** — We do not repeat XNU's mistake.
5. **Everything Documented** — Every design decision, every source reference is documented.

## Beyond the Desktop

The NEOMACH kernel is not limited to desktop use.  The microkernel design enables it to run
on embedded edge devices, industrial PLC controllers, virtual PLC runtimes, and automotive
SoCs — the same kernel binary, different servers.

See [docs/use-cases.md](docs/use-cases.md) for:
- Processor architecture requirements and minimum hardware specs
- RTOS and real-time applicability
- PLC runtime and virtual PLC deployment profiles
- Edge / IoT gateway examples (Raspberry Pi, NXP i.MX, RISC-V)
- Industrial and automotive platform examples

## License

- New NEOMACH kernel code: **GPL-2.0-or-later**
- New NEOMACH servers: **LGPL-2.1-or-later**
- GNUstep submodules: **LGPL-2.1** (see submodule repos)
- Documentation: **CC BY-SA 4.0**
- XNU-derived portions: **APSL 2.0** (see `docs/sources.md`)

## Contributing

See [docs/roadmap.md](docs/roadmap.md) for the development roadmap and
[TASKS.md](TASKS.md) for specific actionable tasks.

---

*NEOMACH (c) 2026 tracey — Free Software · Open Research*
