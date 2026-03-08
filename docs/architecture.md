# NEOMACH System Architecture

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

## Kernel/Server Split

NEOMACH maintains strict microkernel discipline. The boundary:

**Inside the kernel (kernel/ directory):**
- Mach port IPC (`kernel/ipc/`)
- Virtual memory (`kernel/vm/`)
- Tasks and threads (`kernel/kern/`)
- Hardware abstraction (`kernel/platform/`)

**Outside the kernel (servers/ directory):**
- BSD personality (POSIX syscalls, signals, fork)
- Virtual filesystem
- Device drivers
- Network stack
- Authentication/capabilities

## Boot Sequence

1. Bootloader (GRUB2/UEFI) loads kernel image
2. Platform code initializes hardware (GDT, IDT, paging)
3. `kernel/kern/startup.c` initializes Mach primitives
4. Bootstrap server starts, registers well-known ports
5. Initial servers start (device, vfs, bsd, network, auth)
6. `init` process started via BSD server
7. Login / workspace manager

## IPC Flow

All cross-domain communication uses Mach messages on ports:

```
Client Task                    Server Task
    │                              │
    │  mach_msg(SEND, port, msg)   │
    │─────────────────────────────>│
    │                              │  mach_msg(RECEIVE, port)
    │                              │  [process request]
    │  mach_msg(RECEIVE, reply)    │
    │<─────────────────────────────│
```

## Deployment Scope

The NEOMACH kernel is not limited to desktop use.  The microkernel design makes it
applicable to embedded systems, edge devices, industrial PLCs, virtual PLC runtimes,
and more — the kernel binary is identical; only the servers launched at boot change.

See [use-cases.md](use-cases.md) for a comprehensive discussion of:
- Supported and planned processor architectures and minimum hardware requirements
- RTOS / real-time applicability and what is required
- PLC runtime and virtual PLC runtime profiles
- Edge / IoT gateway deployment patterns
- Platform-specific examples (Raspberry Pi, NXP i.MX, RISC-V, Industrial PC, Automotive SoC)
- Decision guide for choosing NEOMACH vs. other microkernel or RTOS options

## Source References

- CMU Mach 3.0 design: `archive/cmu-mach/`
- OSF MK extensions: `archive/osf-mk/`
- HURD server model: https://git.savannah.gnu.org/git/hurd/hurd.git
- XNU reference: https://github.com/apple-oss-distributions/xnu
