# archive/ — Historical Kernel References

This directory contains or references all historical operating system sources that UNHOX draws from. Everything here is **reference material for study and design inspiration** — no new UNHOX code is written here.

The archive serves three purposes:

1. **Design Reference** — understand how real microkernels work
2. **Lineage Documentation** — trace UNHOX ancestors and design debt
3. **Source Attribution** — track all borrowed concepts and patterns

## Contents

### Actively Maintained

| Directory | Source | Size | Use |
|-----------|--------|------|-----|
| `gnu-mach-ref/` | GNU Mach (Linux/Hurd) | 21 MB | **Primary IPC reference** |
| `hurd-ref/` | GNU Hurd servers | 17 MB | **Server architecture reference** |

These are the most accessible, actively-maintained versions of CMU Mach 3.0 concepts.
See below for how to use them.

### Obtained/To Be Obtained

| Directory | Source | Status | Purpose |
|-----------|--------|--------|---------|
| `cmu-mach/` | CMU Mach 3.0 | 📋 README with fetch instructions | Core microkernel concepts |
| `osf-mk/` | OSF/1 MK kernels | 📋 README with context (proprietary) | NORMA IPC, SMP design |
| `utah-oskit/` | Utah Flux project | 📋 README with fetch instructions | Modular OS architecture |
| `next-docs/` | NeXTSTEP/OPENSTEP | 📋 README with Bitsavers links | Framework and GUI design |

For **each directory**, read its `README.md` for:
- What the source code contains
- Where to obtain it (if not already present)
- Which UNHOX subsystems use it
- Key files and design patterns

## Recommended Study Path

### Phase 1: Core Microkernel (IPC, Tasks, Threads)

**Start here:**
```bash
cd archive/gnu-mach-ref/

# Study IPC concepts
less ipc/ipc.c              # Port and message queue management
less ipc/ipc_entry.h        # IPC space (task namespace)
less ipc/ipc_kmsg.c         # Message passing primitives

# Study task/thread model  
less kern/task.c            # Task (address space) lifecycle
less kern/thread.c          # Thread (scheduling unit) management
less kern/sched.c           # Scheduler
```

**Compare to UNHOX:**
```bash
cd ../../kernel

# How does UNHOX differ from Mach?
diff -u ../archive/gnu-mach-ref/ipc/ipc.c ipc/ipc.c | less
diff -u ../archive/gnu-mach-ref/kern/task.c kern/task.c | less
```

**Reference doc:**
- See `docs/ipc-design.md` for UNHOX IPC design rationale
- See `docs/kernel-heritage.md` for design debt to Mach

### Phase 2: Server Architecture (BSD Personality)

**Study:**
```bash
cd archive/hurd-ref/

# How servers communicate with microkernel
less hurd/utils.c           # Hurd utility servers
less hurd/process.c         # Process management  
less hurd/fs/                # Filesystem server

# Compare to Utah Lites (read archive/utah-oskit/README.md)
```

**Apply to UNHOX:**
```bash
cd ../../servers

# UNHOX server design follows Hurd pattern
less bsd/bsd.c              # BSD personality server
less vfs/                    # VFS translator
```

### Phase 3+: Advanced Topics (NORMA, SMP, Display Server)

Each phase builds on core concepts. Refer to specific archive READMEs.

## External References (Not Mirrored Locally)

These are large or proprietary projects useful as references:

| Source | URL | Type | UNHOX Phase |
|--------|-----|------|-------------|
| **GNU Mach** | https://git.savannah.gnu.org/git/hurd/gnumach.git | Active | Phase 1-2 |
| **GNU Hurd** | https://git.savannah.gnu.org/git/hurd/hurd.git | Active | Phase 2-3 |
| **XNU (Darwin)** | https://github.com/apple-oss-distributions/xnu | Proprietary | Phase 3+ |
| **seL4** | https://github.com/seL4/seL4 | Research | Reference |
| **genode** | https://github.com/genodelabs/genode | Research | Reference |

## Documentation Obligation

Per UNHOX design principle:

> **Every borrowed concept must be documented** — source, lineage, and design decisions.

When implementing a subsystem inspired by archive sources:

1. Add comment pointing to reference implementation
   ```c
   /* Inspired by GNU Mach ipc/ipc.c:port_create()
      See archive/gnu-mach-ref/ipc/ipc.c for historical context
   */
   ```

2. Document design differences in RFC
   ```markdown
   # RFC-0002: IPC Port Implementation
   
   Based on CMU Mach 3.0 (see archive/gnu-mach-ref/)
   Simplified for Phase 1: [differences listed]
   ```

3. Update `docs/sources.md` with license attribution

4. Update `docs/kernel-heritage.md` with lineage

## Tools for Archive Exploration

### Clone a Reference
```bash
# If a reference isn't locally mirrored, fetch it
cd archive/
git clone https://git.savannah.gnu.org/git/hurd/gnumach.git gnu-mach-ref
```

### Search for Concepts
```bash
# Find all message passing code in GNU Mach
grep -r "mach_msg" gnu-mach-ref/

# Find scheduling algorithm
grep -r "round_robin" gnu-mach-ref/kern/
```

### Compare UNHOX to References
```bash
# Show how UNHOX task management differs from Mach
diff gnu-mach-ref/kern/task.c ../../kernel/kern/task.c

# Find similar patterns
grep -l "vm_map" gnu-mach-ref/vm/*.c | head -3
```

## Licensing Notes

Different sources have different licenses. See `docs/sources.md` for complete inventory.

**Key licenses:**
- **CMU Mach 3.0** — MIT/CMU permissive  
- **GNU Mach/Hurd** — GNU GPL v2+
- **NeXTSTEP** — Proprietary (archived, preserved for education)
- **Utah OSKit/Lites** — MIT + GPL components

**UNHOX license:** GPL v3+ (compatible with above references)

## See Also

- `docs/sources.md` — License inventory
- `docs/kernel-heritage.md` — UNHOX's design lineage  
- `docs/ipc-design.md` — Custom IPC design vs. Mach
- `docs/bsd-server-design.md` — Server architecture choices
