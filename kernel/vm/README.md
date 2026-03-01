# kernel/vm/

Virtual memory subsystem.

## Overview

Mach's VM design separates the concept of a **memory object** (backing store,
managed by an external pager) from a **VM map entry** (the mapping of that object
into an address space). This indirection enables flexible memory management without
putting pager logic in the kernel.

## Key Abstractions

- **vm_map** — per-task virtual address space
- **vm_object** — backing object with external pager protocol
- **vm_page** — physical page tracking
- **memory_object** — Mach port interface for external pagers

## Implementation Plan

- [ ] `vm_map.h` / `vm_map.c` — address space management
- [ ] `vm_object.h` / `vm_object.c` — memory object lifecycle
- [ ] `vm_page.h` / `vm_page.c` — physical page allocator
- [ ] `pmap.h` — platform-specific page table abstraction (implemented in `platform/`)
- [ ] `vm_fault.c` — page fault handler, calls external pager
- [ ] `memory_object.c` — external pager protocol stubs

## References

- Mach Virtual Memory documentation (CMU)
- GNU Mach `vm/` directory
- XNU `osfmk/vm/`
