/*
 * kernel/platform/aarch64/context_switch.S — AArch64 context switch for UNHOX
 *
 * Saves the callee-saved registers of the current thread and restores
 * those of the next thread, switching execution context.
 *
 * AArch64 ABI callee-saved registers (AAPCS64):
 *   x19–x28  — general purpose, callee-saved
 *   x29      — frame pointer (fp), callee-saved
 *   x30      — link register (lr); here used as saved return address (pc)
 *   sp       — stack pointer (implicitly callee-saved)
 *
 * Caller-saved registers (x0–x18, x29 as scratch, NEON/FP) are already
 * saved on the caller's stack by the C calling convention.
 *
 * struct cpu_state layout (AArch64, 8 bytes each):
 *   offset   0: x19
 *   offset   8: x20
 *   offset  16: x21
 *   offset  24: x22
 *   offset  32: x23
 *   offset  40: x24
 *   offset  48: x25
 *   offset  56: x26
 *   offset  64: x27
 *   offset  72: x28
 *   offset  80: fp  (x29)
 *   offset  88: sp
 *   offset  96: pc  (x30 / link register — return address)
 *
 * Parameters (AAPCS64):
 *   x0 = pointer to from_thread->th_cpu_state (struct cpu_state *)
 *   x1 = pointer to to_thread->th_cpu_state   (struct cpu_state *)
 *
 * Reference: Procedure Call Standard for the Arm® 64-bit Architecture
 *            (AAPCS64), §6.1 — Register roles.
 */

    .section .text
    .globl  context_switch_asm
    .type   context_switch_asm, %function

context_switch_asm:
    /* ================================================================
     * SAVE current thread's callee-saved state into *from (x0)
     * ================================================================ */
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    str     x29,      [x0, #80]   /* frame pointer                          */
    mov     x2, sp
    str     x2,       [x0, #88]   /* stack pointer                          */
    str     x30,      [x0, #96]   /* link register = return address (pc)    */

    /* ================================================================
     * RESTORE next thread's state from *to (x1)
     * ================================================================ */
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldr     x29,      [x1, #80]   /* frame pointer                          */
    ldr     x2,       [x1, #88]   /* stack pointer                          */
    mov     sp, x2
    ldr     x30,      [x1, #96]   /* link register — resume point           */

    /*
     * Return to the restored link register.
     * For a previously switched-out thread: returns into the caller of
     * context_switch_asm() in that thread.
     * For a freshly created thread: jumps to thread_entry_trampoline().
     */
    ret

    .size   context_switch_asm, . - context_switch_asm
