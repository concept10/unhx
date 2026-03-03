/*
 * kernel/platform/boot.S — x86-64 boot entry for UNHOX
 *
 * Multiboot1-compliant boot header with the AOUT_KLUDGE address fields,
 * which allows QEMU's built-in multiboot loader to load this 64-bit ELF
 * without trying to parse the ELF headers (it only supports 32-bit ELFs).
 *
 * Sequence:
 *   1. Multiboot1 header (with address tag) — recognized by GRUB / QEMU
 *   2. _start — entry point; entered in 32-bit protected mode
 *   3. Zero BSS (must happen before page tables are set up, since the
 *      boot page tables live in BSS)
 *   4. Set up identity-mapped page tables covering the first 4 MB
 *   5. Enable PAE, load CR3, enable long mode (EFER.LME), enable paging
 *   6. Far jump into 64-bit code segment (long_mode_entry)
 *   7. Load segment registers and set up a 16 KB stack
 *   8. Call kernel_main() — defined in kernel/kern/kern.c
 *
 * Reference: AMD64 Architecture Programmer's Manual Vol. 2, §14.6;
 *            Multiboot Specification v0.6.96 §3.1.
 */

/* =========================================================================
 * Multiboot1 header constants
 * ========================================================================= */
    .set MB_MAGIC,     0x1BADB002
    .set MB_FLAGS,     0x00010003       /* ALIGN | MEMINFO | AOUT_KLUDGE     */
    .set MB_CHECKSUM,  -(MB_MAGIC + MB_FLAGS)

/* =========================================================================
 * Multiboot1 header with address fields (AOUT_KLUDGE)
 *
 * The address fields tell the bootloader how to load the kernel as raw
 * bytes, bypassing ELF parsing.  This is required because QEMU's built-in
 * multiboot loader rejects 64-bit ELF images.
 * ========================================================================= */
    .section .multiboot, "a"
    .align 4
mb_header:
    .long   MB_MAGIC
    .long   MB_FLAGS
    .long   MB_CHECKSUM
    /* Address fields (valid when bit 16 of flags is set) */
    .long   mb_header               /* header_addr  — where this header is   */
    .long   mb_header               /* load_addr    — start of loadable image*/
    .long   __bss_start             /* load_end_addr— end of data segments   */
    .long   __bss_end               /* bss_end_addr — end of BSS             */
    .long   _start                  /* entry_addr   — 32-bit entry point     */

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
 * Multiboot1 calling convention (32-bit):
 *   EAX = 0x2BADB002  (Multiboot boot magic)
 *   EBX = physical address of Multiboot information structure
 * ========================================================================= */
    .section .text
    .code32
    .globl  _start
    .type   _start, @function
_start:
    cli                              /* disable interrupts — no IDT yet      */

    /*
     * Save EBX (Multiboot info pointer) in ESI; we will pass it to
     * kernel_main() via RDI once we reach 64-bit code.
     */
    movl    %ebx, %esi

    /* ----------------------------------------------------------------
     * Zero the BSS section (in 32-bit mode, before paging).
     *
     * CRITICAL: this must happen before setting up boot page tables,
     * because boot_pml4/boot_pdpt/boot_pd live in BSS.  If we zeroed
     * BSS after enabling paging, we'd destroy the page tables that
     * paging depends on, causing a triple fault.
     * ---------------------------------------------------------------- */
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx              /* byte count                            */
    shrl    $2, %ecx                /* convert to 4-byte dwords              */
    xorl    %eax, %eax
    rep stosl                       /* zero BSS (32-bit stosd)               */

    /* ----------------------------------------------------------------
     * Build minimal identity-mapped page tables (physical == virtual
     * for the first 4 MB) so that paging can be enabled while the CPU
     * is still executing from physical addresses.
     *
     * We use two 2 MB huge pages (PS bit) to keep it simple.
     *
     * PML4[0] → boot_pdpt | PRESENT | WRITE
     * PDPT[0] → boot_pd   | PRESENT | WRITE
     * PD[0]   → 0x000000  | PRESENT | WRITE | HUGE (2 MB page, PS=1)
     * PD[1]   → 0x200000  | PRESENT | WRITE | HUGE (2 MB page, PS=1)
     * ---------------------------------------------------------------- */
    movl    $boot_pdpt, %eax
    orl     $0x03, %eax             /* present + writable                    */
    movl    %eax, boot_pml4         /* PML4[0]                               */

    movl    $boot_pd, %eax
    orl     $0x03, %eax
    movl    %eax, boot_pdpt         /* PDPT[0]                               */

    movl    $0x83, %eax             /* 0x000000 | present | write | huge(PS) */
    movl    %eax, boot_pd           /* PD[0] → first 2 MB (0x000000–0x1FFFFF) */

    movl    $0x200083, %eax         /* 0x200000 | present | write | huge(PS) */
    movl    %eax, boot_pd + 8       /* PD[1] → next 2 MB (0x200000–0x3FFFFF)  */

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

    /*
     * Call kernel_main() — defined in kernel/kern/kern.c.
     *
     * System V AMD64 ABI: first integer argument goes in RDI.
     * We saved the Multiboot info pointer in ESI (32-bit), zero-extend
     * it into RDI so kernel_main() can parse the memory map.
     */
    movl    %esi, %edi              /* Multiboot info ptr (zero-extended)    */
    call    kernel_main

    /* kernel_main() must not return; halt if it does */
.Lhalt:
    cli
    hlt
    jmp     .Lhalt
