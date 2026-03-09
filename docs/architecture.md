# Neomach System Architecture

## Full Software Stack

```
┌─────────────────────────────────────────────────────────────────┐
│               Neomach Full Software Stack                         │
├─────────────────────────────────────────────────────────────────┤
│  Workspace Manager  │  AppKit  │  Display Server (DPS)          │
├─────────────────────┼──────────┼────────────────────────────────┤
│  Foundation Kit  │  libobjc2  │  libdispatch  │  CoreFoundation │
├─────────────────────────────────────────────────────────────────┤
│  AudioUnits framework  │  MIDI Server  │  Audio Server          │
├─────────────────────────────────────────────────────────────────┤
│  BSD Server  │  VFS Server  │  Net Server  │  Auth Server       │
├─────────────────────────────────────────────────────────────────┤
│  Device Server  │  Bootstrap Server  │  Pager Servers           │
├─────────────────────────────────────────────────────────────────┤
│  MACH MICROKERNEL  (IPC · VM · Tasks · Threads · Sched/RT)     │
├─────────────────────────────────────────────────────────────────┤
│  Hardware Abstraction  (x86-64 / AArch64)                       │
└─────────────────────────────────────────────────────────────────┘
```

## Kernel/Server Split

Neomach maintains strict microkernel discipline. The boundary:

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
- Audio Server (HAL, graph, RT mixing)
- MIDI Server (device I/O, event routing)

**Framework layer (frameworks/ directory):**
- Audio Units (plugin model for effects, instruments, converters)
- Objective-C runtime, Foundation, AppKit, libdispatch

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

## Source References

- CMU Mach 3.0 design: `archive/cmu-mach/`
- OSF MK extensions: `archive/osf-mk/`
- HURD server model: https://git.savannah.gnu.org/git/hurd/hurd.git
- XNU reference: https://github.com/apple-oss-distributions/xnu
