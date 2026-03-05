# Utah OSKit + Lites Source Reference

This directory should contain the OSKit and Lites source code from University of Utah Flux project.

## What is OSKit?

**OSKit** is a component-based operating system construction kit (mid-1990s):

### Key Idea
- Traditional OS = monolithic kernel  
- OSKit = **library of composable components**
- Mix-and-match subsystems (VM, threads, filesystems, device drivers)
- Build custom kernels from standard pieces

### Components
- **libc** — standalone C library (no POSIX, no dependencies)
- **Device drivers** — disk, network, USB (driver agnostic)  
- **Filesystem** — UFS, FAT12, ISO9660
- **Thread/VM** — basic execution environment
- **Examples** — boot loaders, minimal kernels, OS prototypes

### Historical Value for UNHOX
- **Modular architecture** — clean OS boundaries
- **Driver abstraction** — decouple hardware from OS logic
- **Minimal dependencies** — build bare-metal kernels
- **Educational resource** — comprehensive, well-documented code

## What is Lites?

**Lites** is a complete BSD 4.4 server running **on top of Mach 3.0**:

### Architecture
- **Mach microkernel** (core)
- **Lites BSD server** (TCP/IP, processes, filesystems)
- **System call emulation** — BSD syscalls → Mach IPC
- **Personality server** — application compatibility layer

### Example Design Pattern
```
User App (execve /bin/sh)
    ↓
[system call: fork]
    ↓
Lites BSD Server (on Mach task)
    ↓
task_create → new Mach task
thread_create → new thread in Mach
    ↓
Child runs in new Mach task
(Lites handles BSD-isms transparently)
```

### Historical Significance
- **Most complete reference** for personality servers on Mach
- Shows how to bootstrap traditional UNIX on microkernel
- Real-world proof that microkernel + servers is viable
- Influences UNHOX BSD server design

## Where to Obtain OSKit + Lites

### Challenges
- Flux project archived (1990s-2003)
- Primary sources down or archived
- Components distributed separately

### Possible Sources

1. **Flux Archive** (may be offline)
   ```
   http://www.cs.utah.edu/flux/oskit/
   http://www.cs.utah.edu/flux/lites/
   ```

2. **GitHub Community Mirrors**
   ```bash
   github.com search: oskit, lites-kernel
   # Some educational forks maintain copies
   ```

3. **Archive.org Wayback Machine**
   ```
   https://web.archive.org/web/*/cs.utah.edu/flux/*
   ```

4. **Bitsavers** (documentation)
   ```
   https://bitsavers.org/ (search: oskit, lites)
   ```

5. **Academic Archives**
   - University of Utah Computer Science Department
   - CMU Computer Systems Lab
   - MIT CSAIL

### Manual Fetch Example

```bash
# Try Wayback Machine for HTML/tarballs
wget -r --accept="*.tar.gz" \
  "https://web.archive.org/web/2003*/cs.utah.edu/flux/oskit/"

# Look for GitHub community versions  
git clone https://github.com/user/oskit-mirror oskit-ref
```

## Key Files to Study

### OSKit Structure
```
oskit/
  ├── libc/              — Standalone C library
  ├── oskit/             — Main kernel headers
  │   ├── dev/           — Device API abstraction
  │   ├── fs/            — Filesystem interface
  │   ├── io/            — Device I/O
  │   └── kern/          — Kernel services
  ├── fs/                — Filesystem implementations (UFS, FAT, etc.)
  ├── dev/               — Device drivers (disk, net, USB)
  └── examples/          — Example kernels (boot, diskboot, etc.)
```

### Lites Structure
```
lites/
  ├── kernel/            — Mach server implementation
  │   ├── ipc.c          — System call IPC  
  │   ├── process.c      — Process management
  │   ├── vm.c           — Memory management
  │   └── device.c       — Device syscalls
  ├── server/            — BSD system call handlers
  ├── libc/              — BSD libc portion
  └── boot/              — Bootstrap code
```

## Design Patterns for UNHOX

### 1. Component-Based Architecture
- Keep filesystem, device, VM logic separate
- Exchange through well-defined interfaces
- Enables testing, reuse, swapping

### 2. Personality Servers
- Lites shows how to implement BSD-compatible layer
- UNHOX can follow similar pattern:
  - Mach microkernel (core)
  - BSD server (process, syscall emulation)
  - VFS server (filesystem)
  - Device server (drivers)

### 3. Modular Drivers  
- OSKit driver model: hardware-agnostic interface
- Drivers pluggable (no OS-specific code)
- Easier cross-platform support

## UNHOX References

Inspired by OSKit + Lites architecture:

- `servers/bsd/` — UNHOX BSD personality server  
- `servers/vfs/` — VFS abstraction layer
- `servers/device/` — Device management server
- `docs/bsd-server-design.md` — Design rationale
- `kernel/ipc/` — Mach-like IPC infrastructure

## See Also

- University of Utah Flux: https://en.wikipedia.org/wiki/Flux_(operating_system)
- OSKit documentation: http://www.cs.utah.edu/flux/oskit/html/
- Lites overview: http://www.cs.utah.edu/flux/lites/
- Microkernel architecture: https://www.wikiwand.com/en/Microkernel
- GNU Hurd (modern successor): https://www.gnu.org/software/hurd/

## License

OSKit: MIT/BSD-style permissive license
Lites: GPL v2+ (due to BSD kernel components)

- Utah Flux archives: http://www.cs.utah.edu/flux/oskit/
- Lites: http://www.cs.utah.edu/flux/lites/

## License

BSD-style (see individual file headers).

## Files to Mirror

- [ ] `oskit/` — component framework headers and core libraries
- [ ] `lites/` — BSD server on Mach 3.0 full source
- [ ] Utah Mach4 patches — i386 bring-up improvements over CMU Mach 3.0
