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

## Current Status — Phase 1 Complete (partial)

| File | Status | Description |
|------|--------|-------------|
| `vm_page.h` / `vm_page.c` | ✅ Done | Physical page frame allocator; initialized from Multiboot memory map |
| `vm.h` / `vm.c` | ✅ Done | VM subsystem init (`vm_init`); wires `vm_page_alloc` into the paging layer |
| `vm_map.h` | ✅ Done | Per-task address space interface (Phase 1 stubs) |
| `vm_map.c` | 🔲 Phase 2 | Full address space management — map/unmap/protect |
| `vm_object.h` / `vm_object.c` | 🔲 Phase 2 | Memory object lifecycle |
| `vm_fault.c` | 🔲 Phase 2 | Page fault handler, calls external pager |
| `memory_object.c` | 🔲 Phase 2 | External pager protocol stubs |

## Phase 2 TODO

- [ ] `vm_map.c` — per-task address space management
- [ ] `vm_object.c` — memory object lifecycle
- [ ] `vm_fault.c` — page fault handler
- [ ] `memory_object.c` — external pager protocol stubs
- [ ] Verify: user task runs in its own address space
- [ ] Unit test: allocate and free physical pages
- [ ] Unit test: vm_map create/destroy

## References

- Mach Virtual Memory documentation (CMU)
- GNU Mach `vm/` directory
- XNU `osfmk/vm/`
