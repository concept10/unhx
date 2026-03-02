# UNHU Kernel — Development History

## First Successful Boot — 2026-03-01

The UNHU kernel achieved its first successful boot on March 1, 2026,
running on QEMU x86-64 (TCG) from an arm64 M3 MacBook Air.

### Boot output

```
================================================
  UNHU — U Is Not Hurd, it's µ
  Mach microkernel — Phase 1
================================================
[UNHU] kernel_main entered
[UNHU] initialising kernel heap...
[UNHU] initialising IPC subsystem...
[UNHU] initialising VM subsystem...
[UNHU] initialising kernel core...
[UNHU] kernel task (task 0) created
[UNHU] all subsystems initialised

[UNHU IPC] beginning IPC smoke test...
[UNHU IPC] task_a and task_b created
[UNHU IPC] port allocated in task_a (receive right)
[UNHU IPC] send right granted to task_b
[UNHU IPC] task_b sent message
[UNHU IPC] message received: hello
[UNHU IPC] magic: 0x00000000deadbeef (correct)
[UNHU] Phase 1 complete. Mach IPC operational.

[bootstrap] initialising bootstrap server
[bootstrap] registered: com.unhu.kernel
[bootstrap] registered: com.unhu.ipc_test
[bootstrap] lookup com.unhu.kernel → port 1 (OK)
[bootstrap] lookup com.unhu.nonexistent → not found (OK)
[bootstrap] duplicate registration rejected (OK)
[bootstrap] bootstrap server ready

========================================
 UNHU IPC Milestone Test (v0.2)
========================================
  [PASS] task_a created
  [PASS] task_b created
  [PASS] task_a has ipc_space
  [PASS] task_b has ipc_space
  [PASS] alloc port name in task_a
  [PASS] port object allocated
  [PASS] alloc port name in task_b
  [PASS] mach_msg_send returns KERN_SUCCESS
  [PASS] mach_msg_receive returns KERN_SUCCESS
  [PASS] received magic == 0xDEADBEEF
  [PASS] received message == "phase1_ok"
  [PASS] received size matches sent size
  received magic: 0x00000000deadbeef
  received text:  phase1_ok
  [PASS] send without right returns error
========================================
 Results: 13 passed, 0 failed, 13 total
 STATUS: PASS
========================================
[UNHU] IPC milestone v0.2 PASSED.

[UNHU] All milestone tests PASSED.

[UNHU] halting (cooperative scheduling only in Phase 1)
```

### Build environment

- **Host**: macOS arm64 (Apple M3)
- **Compiler**: LLVM Clang 22.1.0 (`brew install llvm`) with `--target=x86_64-unknown-elf`
- **Linker**: ld.lld (`brew install lld`)
- **Emulator**: QEMU 10.2.1 (`brew install qemu`), TCG acceleration
- **Build system**: CMake 3.20+ with custom cross-compilation toolchain

### QEMU invocation

```
qemu-system-x86_64 -kernel build/unhx.elf -no-reboot -display none \
    -serial file:/tmp/unhx_serial.log -m 64M
```

### Obstacles overcome to reach first boot

1. **arm64 host cannot assemble x86-64 natively** — Apple Clang rejected x86-64
   instructions (`movq`, `cli`, `hlt`). Fixed by creating a cross-compilation
   toolchain (`cmake/x86_64-elf-clang.cmake`) using brew LLVM with
   `--target=x86_64-unknown-elf`.

2. **macOS linker flags injected into ld.lld** — CMake's default link command
   uses the clang driver, which on macOS injects `-arch`, `-platform_version`,
   and `-syslibroot`. Fixed by overriding `CMAKE_C_LINK_EXECUTABLE` to invoke
   ld.lld directly.

3. **Compiler flag leaking to linker** — The `<FLAGS>` placeholder in the
   custom link command passed `--target=x86_64-unknown-elf` to ld.lld (a
   compiler flag, not a linker flag). Fixed by removing `<FLAGS>` from the
   link command template.

4. **R_X86_64_32 relocation overflow** — The linker script placed kernel
   sections at VIRT_BASE (0xFFFFFFFF80000000) for a higher-half layout, but
   boot.S uses 32-bit absolute addresses in `.code32` mode. These addresses
   exceeded the unsigned 32-bit range. Fixed by switching to physical-address
   linking at PHYS_BASE (0x100000) for Phase 1, deferring higher-half to
   Phase 2.

5. **QEMU rejects 64-bit ELF** — QEMU's built-in Multiboot1 loader only
   accepts 32-bit ELF files (`Cannot load x86-64 image, give a 32bit one`).
   Fixed by adding the AOUT_KLUDGE flag (bit 16) to the Multiboot1 header
   with explicit address fields, which tells QEMU to load raw bytes instead
   of parsing the ELF format.

6. **Triple fault during BSS zeroing** — The boot page tables
   (`boot_pml4`, `boot_pdpt`, `boot_pd`) reside in the BSS section. The
   original boot sequence zeroed BSS *after* enabling paging, which destroyed
   the identity mapping and caused a page fault cascade (page fault → double
   fault → triple fault → reboot). Fixed by moving BSS zeroing to 32-bit
   mode *before* page table setup and paging enablement.

### What Phase 1 demonstrates

- Multiboot1 boot from QEMU with 32-bit → 64-bit long mode transition
- 4-level paging with 2 MB identity map
- Kernel heap (256 KB bump allocator)
- Physical page frame allocator
- Mach IPC: ports, port rights, port name spaces, message queues
- `mach_msg_send()` / `mach_msg_receive()` kernel entry points
- Task and thread abstractions
- Round-robin scheduler (cooperative, no preemption yet)
- Bootstrap server with name → port registry
- 13/13 IPC milestone tests passing
