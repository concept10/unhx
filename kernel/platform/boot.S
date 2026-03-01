/*
 * kernel/platform/boot.S — x86-64 boot entry for UNHOX
 *
 * This file implements the Multiboot2-compliant boot header and the initial
 * machine state setup required before C code can run.
 *
 * Conforms to: The Multiboot2 Specification, version 2.0
 *   (https://www.gnu.org/software/grub/manual/multiboot2/multiboot2.html)
 *
 * Sequence:
 *   1. Multiboot2 header — recognized by GRUB/QEMU direct-boot
 *   2. _start — entry point; GRUB enters here in 32-bit protected mode
 *   3. Set up minimal identity-mapped page tables covering the first 2 MB
 *   4. Enable PAE, load CR3, enable long mode (EFER.LME), enable paging
 *   5. Far jump into 64-bit code segment (long_mode_entry)
 *   6. Load segment registers and set up a 16 KB stack
 *   7. Zero the BSS section
 *   8. Call kernel_main() — defined in kernel/kern/kern.c
 *
 * NOTE: Standard GRUB 2 with Multiboot2 enters in 32-bit protected mode even
 *       on a 64-bit machine, so we perform the 32→64 long-mode transition
 *       ourselves here.
 *
 * Reference: AMD64 Architecture Programmer's Manual Vol. 2, §14.6 —
 *            Enabling Long Mode.
 *            Multiboot2 Specification §3.1 — OS image format.
 */

/* =========================================================================
 * Multiboot2 header constants
 * ========================================================================= */
    .set MB2_MAGIC,      0xE85250D6
    .set MB2_ARCH_I386,  0               /* 32-bit (x86) protected mode      */
    .set MB2_HDR_LEN,    (mb2_header_end - mb2_header_start)
    .set MB2_CHECKSUM,   -(MB2_MAGIC + MB2_ARCH_I386 + MB2_HDR_LEN)

/* =========================================================================
 * Multiboot2 header — must appear within the first 32 KB of the image.
 * GRUB scans for MB2_MAGIC (0xE85250D6) to identify a Multiboot2 kernel.
 * ========================================================================= */
    .section .multiboot2, "a"
    .align 8
mb2_header_start:
    .long   MB2_MAGIC
    .long   MB2_ARCH_I386
    .long   MB2_HDR_LEN
    .long   MB2_CHECKSUM
    /* End tag (required): type=0, flags=0, size=8 */
    .word   0
    .word   0
    .long   8
mb2_header_end:

/* =========================================================================
 * BSS: initial kernel stack (16 KB) and boot page tables
 * ========================================================================= */
    .section .bss
    .align 4096
boot_pml4:  .skip 4096
boot_pdpt:  .skip 4096
boot_pd:    .skip 4096

    .align 16
stack_bottom:
    .skip   16384                   /* 16 KB initial kernel stack            */
stack_top:

/* =========================================================================
 * Temporary GDT for the 32→64 long-mode transition
 *
 * Three entries: null, 64-bit code (L=1), 64-bit data.
 * gdt_init() in gdt.c rebuilds a permanent GDT after kernel_main() starts.
 * ========================================================================= */
    .section .rodata
    .align 8
boot_gdt:
    .quad   0x0000000000000000       /* 0x00 — null descriptor               */
    .quad   0x00AF9A000000FFFF       /* 0x08 — 64-bit code: P=1, DPL=0, L=1 */
    .quad   0x00CF92000000FFFF       /* 0x10 — 64-bit data: P=1, DPL=0, R/W */
boot_gdt_end:

boot_gdt_desc:
    .word   boot_gdt_end - boot_gdt - 1
    .long   boot_gdt

/* =========================================================================
 * 32-bit protected-mode entry point
 *
 * GRUB Multiboot2 calling convention (32-bit):
 *   EAX = 0x36D76289  (Multiboot2 boot magic)
 *   EBX = physical address of Multiboot2 information structure
 * ========================================================================= */
    .section .text
    .code32
    .globl  _start
    .type   _start, @function
_start:
    cli                              /* disable interrupts — no IDT yet      */

    /*
     * Save EBX (Multiboot2 info pointer) in ESI; we will pass it to
     * kernel_main() via RDI once we reach 64-bit code.
     */
    movl    %ebx, %esi

    /* ----------------------------------------------------------------
     * Build minimal identity-mapped page tables (physical == virtual
     * for the first 2 MB) so that paging can be enabled while the CPU
     * is still executing from physical addresses in the low 2 MB.
     *
     * We use one 2 MB huge page (PS bit) to keep it simple.
     *
     * PML4[0] → boot_pdpt | PRESENT | WRITE
     * PDPT[0] → boot_pd   | PRESENT | WRITE
     * PD[0]   → 0x000000  | PRESENT | WRITE | HUGE (2 MB page, PS=1)
     * ---------------------------------------------------------------- */
    movl    $boot_pdpt, %eax
    orl     $0x03, %eax             /* present + writable                    */
    movl    %eax, boot_pml4         /* PML4[0]                               */

    movl    $boot_pd, %eax
    orl     $0x03, %eax
    movl    %eax, boot_pdpt         /* PDPT[0]                               */

    movl    $0x83, %eax             /* 0x000000 | present | write | huge(PS) */
    movl    %eax, boot_pd           /* PD[0] → first 2 MB                   */

    /* ----------------------------------------------------------------
     * Enable Physical Address Extension (required for 64-bit paging):
     * CR4.PAE = bit 5
     * ---------------------------------------------------------------- */
    movl    %cr4, %eax
    orl     $0x20, %eax
    movl    %eax, %cr4

    /* Load CR3 with the address of the PML4 table */
    movl    $boot_pml4, %eax
    movl    %eax, %cr3

    /* ----------------------------------------------------------------
     * Enable Long Mode in the EFER MSR (MSR 0xC0000080):
     * EFER.LME = bit 8
     * ---------------------------------------------------------------- */
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $0x100, %eax
    wrmsr

    /* ----------------------------------------------------------------
     * Load the temporary 64-bit GDT, then enable paging + PE in CR0.
     * Setting CR0.PG while EFER.LME=1 activates long mode (IA-32e mode).
     * ---------------------------------------------------------------- */
    lgdtl   boot_gdt_desc

    movl    %cr0, %eax
    orl     $0x80000001, %eax       /* PG=bit31, PE=bit0                     */
    movl    %eax, %cr0

    /*
     * Far jump to reload CS with the 64-bit code descriptor (selector 0x08).
     * This is mandatory: after setting CR0.PG the CPU is in compatibility
     * mode until CS is reloaded with an L=1 descriptor.
     */
    ljmpl   $0x08, $long_mode_entry

/* =========================================================================
 * 64-bit long-mode entry
 * ========================================================================= */
    .code64
long_mode_entry:
    /* Load all data segment registers with the 64-bit data selector (0x10) */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* Establish the initial C stack (grows downward from stack_top) */
    movq    $stack_top, %rsp

    /* ----------------------------------------------------------------
     * Zero the BSS section.
     * The linker script (linker.ld) exports __bss_start and __bss_end.
     * We clear it before any C code runs so that uninitialized globals
     * are guaranteed to be zero, as the C standard requires.
     * ---------------------------------------------------------------- */
    movq    $__bss_start, %rdi
    movq    $__bss_end,   %rcx
    subq    %rdi, %rcx              /* number of bytes to zero               */
    xorl    %eax, %eax              /* fill value = 0                        */
    shrq    $3, %rcx                /* convert bytes → 8-byte quads          */
    rep stosq

    /*
     * Call kernel_main() — defined in kernel/kern/kern.c.
     *
     * System V AMD64 ABI: first integer argument goes in RDI.
     * We saved the Multiboot2 info pointer in ESI (32-bit), zero-extend
     * it into RDI so kernel_main() can parse the memory map.
     *
     * The stack is already 16-byte aligned (stack_top is 16-byte aligned
     * per .align 16 above; call pushes an 8-byte return address making it
     * 8-byte aligned, which is correct — the callee sees RSP%16 == 8
     * on function entry per the ABI).
     */
    movl    %esi, %edi              /* Multiboot2 info ptr (zero-extended)   */
    call    kernel_main

    /* kernel_main() must not return; halt if it does */
.Lhalt:
    cli
    hlt
    jmp     .Lhalt
