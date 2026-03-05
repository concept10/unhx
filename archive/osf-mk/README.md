# OSF MK6/MK7 Source Reference

This directory contains or should contain OSF/1 microkernel sources (MK6/MK7 variants).

## What is OSF MK?

OSF/1 was an enterprise UNIX operating system from the Open Software Foundation (1993-2001).
Its kernel was based on CMU Mach 3.0 with significant enhancements:

### Key Extensions Over CMU Mach 3.0
- **NORMA (Network of Reliable Machines)** — network-transparent IPC
- **Realtime Scheduling** — priority scheduling for real-time tasks  
- **Symmetric Multiprocessor (SMP)** — multi-CPU support
- **Enhanced VM** — improved paging and cache management
- **Device Kit** — advanced device driver framework

### Historical Significance
- OSF/1 was the reference UNIX for Digital Equipment Corporation (DEC)
- Later became Tru64 UNIX (DEC's high-end workstation/server OS)
- Microarchitecture influenced later commercial UNIX variants

## Source Structure

Expected directories (if obtained):
- `kern/` — Kernel core (task, thread, scheduling)
- `ipc/` — IPC and ports (extends CMU Mach)
- `vm/` — Virtual memory with enhancements
- `device/` — Device kit and drivers
- `norma/` — NORMA networking support

## Where to Obtain OSF MK Sources

### Challenges

OSF MK sources are **difficult to obtain** because:
- OSF/1 was proprietary software
- Source licenses were restricted
- Company dissolved in 2005
- Remaining archives are rare

### Possible Sources

1. **MkLinux Archives** (Linux port of OSF MK)
   - https://github.com/search?q=mklinux
   - MkLinux was official OSF/1→Linux port for PowerPC
   - Source shares kernel code with OSF MK

2. **Academic Archives**
   - University of Illinois, Carnegie Mellon (may have research copies)
   - Contact computer science departments directly

3. **Bitsavers** (incomplete, PDFs only)  
   - https://bitsavers.org/bits/DEC/
   - Mostly documentation, not full source

4. **GitHub Community Mirrors**
   ```bash
   github.com search: mklinux, osf-mach, tru64
   ```

### Alternative: Study GNU Mach + Extensions

Since OSF MK is unavailable, study CMU Mach extensions in:

- **GNU Mach** (See `archive/gnu-mach-ref/`)
- **GNU Hurd** (See `archive/hurd-ref/`) — RPC-based successor  
- **darbat/L4** research projects — modern microkernel variants

## Key Design Concepts (Applicable to UNHOX)

### 1. Real-time Scheduling
- Priority-based scheduling (hard deadlines)
- Preemption mechanisms
- Avoid priority inversion

### 2. SMP Support
- Spinlocks for kernel synchronization
- Per-CPU scheduling queues
- Memory barriers and cache coherence

### 3. NORMA (Network IPC)
- Port mobility across networks
- Network-transparent send/receive
- Remote request forwarding
- Cluster-wide task management

## UNHOX Reference

UNHOX focuses on single-machine IPC (not NORMA/networked), but studies:

- `kernel/ipc/` — Mach-like ports and message passing  
- `docs/ipc-design.md` — Design rationale
- Real-time scheduling (Phase 3 enhancement)
- SMP support (Phase 4 enhancement)

## See Also

- OSF/1 Wikipedia: https://en.wikipedia.org/wiki/OSF/1
- Tru64 UNIX history: https://en.wikipedia.org/wiki/Tru64_UNIX
- MkLinux project: https://en.wikipedia.org/wiki/MkLinux
- GNU Hurd (successor concept): https://www.gnu.org/software/hurd/
- L4 microkernel research: https://www.l4-x.org/

## License

OSF/1 was proprietary. GNU Hurd replacement code (successor) is GPL v2+.
- OSF/1 operating system (ran on Alpha, PA-RISC)

## Source Locations

- OSF/RI archives (partially available)
- MkLinux sources contain OSF MK portions: https://github.com/mklinux-project/

## License

Varies — OSF/RI license for OSF portions; see individual file headers.

## Key Components to Study

- [ ] NORMA IPC — distributed port model
- [ ] RT extensions — deadline scheduling
- [ ] SMP locking model — reference for UNHOX SMP support
- [ ] OSF/1 BSD server — reference for UNHOX BSD server
