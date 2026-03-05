# CMU Mach 3.0 Source Reference

This directory should contain the CMU Mach 3.0 kernel source code - the foundational microkernel upon which UNHOX is designed.

## What is Mach?

CMU Mach (version 3.0) is a microkernel developed at Carnegie Mellon University (1985-1994):

- **Core abstraction**: Tasks, threads, ports, and messages
- **IPC foundation**: Mach message passing protocol  
- **Virtual memory**: VM objects and paging support
- **Device management**: Unified device driver model
- **Historical impact**: Foundation for NeXTSTEP, MacOS (XNU), and GNU Hurd

## Key Source Files to Reference

### IPC Subsystem
- `ipc/ipc.c` — Port creation, lifecycle, rights management
- `ipc/ipc_entry.h` — IPC space (task's name space)
- `ipc/ipc_mqueue.c` — Message queues
- `ipc/ipc_kmsg.c` — Kernel messages (mach_msg_send/receive)

### Kernel Core  
- `kern/task.c` — Task management (process abstraction)
- `kern/thread.c` — Thread management and context switching
- `kern/sched.c` — Round-robin scheduler
- `kern/exception.c` — Hardware exception handling

### Virtual Memory
- `vm/vm_map.c` — Virtual address space management
- `vm/vm_object.c` — Backing store objects  
- `vm/vm_page.c` — Physical page frame allocator
- `vm/vm_fault.c` — Page fault handling

## Where to Obtain CMU Mach 3.0

### Primary Sources (May Be Archived)

1. **Flux/Utah Archive** — most historical sources
   ```
   http://www.cs.utah.edu/flux/mach4/
   ```

2. **Bitsavers** — PDFs and source archives
   ```
   https://bitsavers.org/bits/CMU/
   https://bitsavers.org/pdf/cmu/
   ```

3. **Archive.org** — historical snapshots
   ```
   https://archive.org/search.php?query=mach+kernel
   ```

### Recommended: Use GNU Mach (Actively Maintained)

**Best approach for UNHOX development:**

Use **GNU Mach** - a modern, maintained version of CMU Mach 3.0:

```bash
# Already cloned in archive/gnu-mach-ref/
# See: https://git.savannah.gnu.org/cgit/hurd/gnumach.git/
```

GNU Mach is:
- Fully API-compatible with CMU Mach 3.0
- Actively maintained (GNU Hurd project)
- Modern toolchain support (Clang, recent GCC)
- Comprehensive Hurd project documentation
- Accessible on GitHub and GNU Savannah

The code is nearly identical to CMU Mach 3.0, with enhancements for:
- Modern compiler support
- Linux compatibility layers
- Complete version control history
- Active community support

## Manual Fetch (if needed)

### Option 1: Clone GNU Mach directly
```bash
git clone https://git.savannah.gnu.org/git/hurd/gnumach.git mach-ref
```

### Option 2: Archive from Bitsavers
```bash
# Requires manual browser download from:
# https://bitsavers.org/pdf/cmu/

# For PDFs/scans of original documentation
```

### Option 3: Request from Archive.org
```bash
# Some versions available via Wayback Machine
# https://web.archive.org/web/*/cs.utah.edu/flux/mach4/*
```

## Integration into UNHOX

Rather than a line-for-line port, UNHOX studies and adapts these Mach concepts:

1. **IPC Message Protocol**
   - Port naming and capability rights
   - Message queue semantics
   - Send/receive blocking behavior

2. **Task/Thread Distinction**
   - Task = isolated execution context (address space)
   - Thread = kernel scheduling unit  
   - Interaction via task_copy() and thread creation

3. **Virtual Memory Design**
   - VM object hierarchies
   - Copy-on-write optimization
   - Page fault resolution through object chains

4. **Exception Handling**
   - Hardware exceptions → exception messages
   - Task-specific exception handler ports
   - Signal-like notification via IPC

## UNHOX References

Implementation inspired by Mach concepts:

- `kernel/ipc/` — UNHOX IPC subsystem
- `kernel/kern/task.c` — Task management  
- `kernel/kern/thread.c` — Thread scheduling
- `kernel/vm/` — Virtual memory system
- `docs/ipc-design.md` — Design rationale
- `docs/kernel-heritage.md` — Historical context

## See Also

- GNU Hurd: https://www.gnu.org/software/hurd/
- Hurd documentation: https://www.gnu.org/software/hurd/docs.html
- Mach IPC reference: https://www.gnu.org/software/mach/manual/
- GNU Mach source: See `archive/gnu-mach-ref/`
- GNU Hurd source: See `archive/hurd-ref/`

## License

CMU Mach 3.0 is licensed under MIT/CMU permissive license.
GNU Mach derivatives under GPL v2+ (see respective source headers).

## Key Documents to Obtain

- [ ] Mach 3.0 Kernel Principles (CMU Technical Report)
- [ ] Mach 3.0 Server Writer's Guide
- [ ] Mach 3.0 Kernel Interface Reference
- [ ] CMU TR-88-43: "Mach: A New Kernel Foundation for Unix Development"
- [ ] CMU TR-87-154: "The IPC Abstraction"

## Source Files to Mirror

- [ ] `mach3/kernel/` — core kernel source
- [ ] `mach3/include/mach/` — public Mach interface headers
- [ ] `mach3/bootstrap/` — bootstrap server
