/*
 * kernel/platform/aarch64/boot.S — AArch64 boot entry for NEOMACH
 *
 * Entry point for QEMU's -machine virt board.  QEMU loads the ELF image and
 * begins execution at _start (EL1 when launched with -machine virt,
 * virtualization=off, or after the EL2→EL1 drop below).
 *
 * Sequence:
 *   1. Mask all asynchronous exceptions (IRQ, FIQ, SError, Debug)
 *   2. If running at EL2, drop to EL1
 *   3. Set up the initial C stack (stack_top, defined in BSS below)
 *   4. Zero the BSS section (8 bytes at a time)
 *   5. Call kernel_main() — defined in kernel/kern/kern.c
 *   6. Loop on WFI if kernel_main() returns (should not happen)
 *
 * Reference: ARM Architecture Reference Manual (ARMv8-A), §D1 — Exception Model;
 *            QEMU virt machine memory map.
 */

    .section .text.boot, "ax"
    .globl  _start
    .type   _start, %function
_start:
    /*
     * Mask all asynchronous exceptions (DAIF = Debug | SError | IRQ | FIQ).
     * No interrupt handlers are installed yet; any taken exception would be
     * catastrophic.
     */
    msr     daifset, #0xf

    /*
     * Detect current Exception Level.
     * QEMU -machine virt may start in EL2 (default with virtualization
     * extensions).  We drop to EL1 so the kernel runs at the expected
     * privilege level.
     */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2              /* CurrentEL[3:2] → x0                  */
    cmp     x0, #2
    b.ne    .Lel1_setup             /* already at EL1 — skip transition      */

    /*
     * Configure EL1 execution state while still at EL2:
     *   HCR_EL2.RW = 1 → EL1 is AArch64
     */
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)     /* HCR_EL2.RW                           */
    msr     hcr_el2, x0

    /*
     * Set SPSR_EL2 so that eret returns to EL1h (SP_EL1) with all
     * exceptions masked (matching our daifset above).
     *   SPSR_EL2 = EL1h (0b0101) | DAIF masked (bits [9:6] = 0b1111)
     */
    mov     x0, #0x3c5              /* EL1h + DAIF masked                   */
    msr     spsr_el2, x0

    /* ELR_EL2 — where eret jumps: the rest of this stub, now in EL1 */
    adr     x0, .Lel1_setup
    msr     elr_el2, x0
    eret

.Lel1_setup:
    /*
     * Set up the initial kernel stack.
     * SP_EL1 (aliased as SP at EL1) grows downward from stack_top.
     */
    ldr     x0, =stack_top
    mov     sp, x0

    /*
     * Zero the BSS section (8 bytes per iteration).
     * __bss_start and __bss_end are provided by the linker script.
     */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
.Lbss_zero:
    cmp     x1, x2
    b.ge    .Lbss_done
    str     xzr, [x1], #8
    b       .Lbss_zero
.Lbss_done:

    /* Call kernel_main() — no arguments required for Phase 1 */
    bl      kernel_main

    /* kernel_main() must not return; spin on WFI if it does */
.Lhalt:
    wfi
    b       .Lhalt

    .size   _start, . - _start

/* =========================================================================
 * BSS: initial kernel stack (16 KB)
 * ========================================================================= */
    .section .bss
    .align  4

    .skip   16384                   /* 16 KB initial kernel stack            */
stack_top:
