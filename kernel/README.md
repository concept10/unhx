# kernel/

UNHOX Mach microkernel — new implementation.

This is the core of UNHOX: a true Mach microkernel implementing exactly the minimum
required inside the kernel boundary. Design is guided by CMU Mach 3.0 and OSF MK series.

## Subdirectories

| Directory      | Contents |
|---------------|----------|
| `ipc/`        | Mach port IPC — message queues, port rights, port sets |
| `vm/`         | Virtual memory subsystem — maps, objects, external pager protocol |
| `kern/`       | Tasks, threads, scheduler, host/processor abstractions |
| `platform/`   | Hardware Abstraction Layer — x86_64 and AArch64 bring-up |

## Phase 1 Target Primitives

- [ ] Mach port creation and rights management
- [ ] IPC message send/receive
- [ ] Task and thread create/terminate
- [ ] Basic round-robin scheduler
- [ ] Virtual memory maps and objects
- [ ] External pager protocol stubs
- [ ] Bootstrap server (initial service name registration)
- [ ] x86-64 boot via Multiboot2 / UEFI
- [ ] Serial console output

## Reference Sources

- CMU Mach 3.0 — `archive/cmu-mach/`
- OSF MK6/MK7 — `archive/osf-mk/`
- GNU Mach: https://git.savannah.gnu.org/git/hurd/gnumach.git
- XNU osfmk/: https://github.com/apple-oss-distributions/xnu

## License

New UNHOX kernel code: **GPL-2.0-or-later** (compatible with GNU Mach sources)
