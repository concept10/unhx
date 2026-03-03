# UNHOX — U Is Not Hurd Or X

> *The kernel Mach always wanted to become.*

UNHOX is an open-source research and implementation project building a true
running microkernel operating system grounded in the original design philosophy
of the Mach kernel.

**This is not a clone of Linux. It is not a fork of XNU. It is not HURD.**
It draws from all of them and attempts a clean, principled synthesis: a modern,
bootable Mach-based OS with a full software stack including the NeXT/OpenStep
frameworks and desktop environment.

---

## Repository Structure

```
unhox/
├── kernel/           # UNHOX Mach microkernel (new code)
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
git clone --recurse-submodules https://github.com/concept10/unhx.git

# Or initialize submodules after cloning
git submodule update --init --recursive
```

## Current Status

**Phase 1 — Kernel Core**: complete and booting under QEMU.

- [x] Kernel boots and prints serial banner (v0.1)
- [x] Mach IPC milestone test passes (v0.2)
- [x] Bootstrap server register/lookup works (v0.3)
- [x] Userspace `init.elf` launches and runs

**Phase 2 — System Servers**: in progress.

- [x] Hand-written IPC message protocols for bootstrap/VFS/BSD
- [x] VFS ramfs server thread (open/read/close path)
- [x] BSD server thread (serial-backed fd 0/1/2)
- [x] Minimal interactive shell prompt (`unhox$`) appears on serial console (v0.5 shell prompt criterion)
- [ ] BSD process model (`fork/exec/wait/signals`) and full POSIX syscall surface
- [ ] VFS write/stat/readdir/mkdir/unlink and `/bin/sh` execution path

See [TASKS.md](TASKS.md) for the detailed, phase-by-phase checklist.

## Full Software Stack

```
┌─────────────────────────────────────────────────────────────┐
│            UNHOX Full Software Stack                        │
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
| v0.5 | Shell | A shell prompt running on UNHOX |
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

## License

- New UNHOX kernel code: **GPL-2.0-or-later**
- New UNHOX servers: **LGPL-2.1-or-later**
- GNUstep submodules: **LGPL-2.1** (see submodule repos)
- Documentation: **CC BY-SA 4.0**
- XNU-derived portions: **APSL 2.0** (see `docs/sources.md`)

## Contributing

See [docs/roadmap.md](docs/roadmap.md) for the development roadmap and
[TASKS.md](TASKS.md) for specific actionable tasks.

---

*UNHOX (c) 2026 tracey — Free Software · Open Research*
