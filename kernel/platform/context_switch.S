/*
 * kernel/platform/context_switch.S — x86-64 context switch for NEOMACH
 *
 * Saves the callee-saved registers of the current thread and restores
 * those of the next thread, effectively switching execution context.
 *
 * System V AMD64 ABI callee-saved registers:
 *   RBX — general purpose
 *   RBP — frame pointer
 *   R12 — general purpose
 *   R13 — general purpose
 *   R14 — general purpose
 *   R15 — general purpose
 *   RSP — stack pointer (implicitly saved/restored)
 *
 * Caller-saved registers (RAX, RCX, RDX, RSI, RDI, R8-R11) are already
 * saved on the caller's stack by the C calling convention before we get
 * here, so we don't need to save them.
 *
 * Parameters (System V AMD64 ABI calling convention):
 *   RDI = pointer to from_thread->th_cpu_state (struct cpu_state *)
 *   RSI = pointer to to_thread->th_cpu_state   (struct cpu_state *)
 *
 * struct cpu_state layout (each field is 8 bytes):
 *   offset  0: rbx
 *   offset  8: rbp
 *   offset 16: r12
 *   offset 24: r13
 *   offset 32: r14
 *   offset 40: r15
 *   offset 48: rsp
 *   offset 56: rip
 *
 * Reference: System V AMD64 ABI §3.2 — Function Calling Convention;
 *            AMD64 Architecture Programmer's Manual Vol. 1 §3.3.
 */

    .section .text
    .globl  context_switch_asm
    .type   context_switch_asm, @function

context_switch_asm:
    /* ================================================================
     * SAVE current thread's state into from->th_cpu_state (pointed by RDI)
     * ================================================================ */

    /* Save callee-saved general-purpose registers */
    movq    %rbx,  0(%rdi)     /* save RBX — callee-saved GP register       */
    movq    %rbp,  8(%rdi)     /* save RBP — frame pointer                  */
    movq    %r12, 16(%rdi)     /* save R12 — callee-saved GP register       */
    movq    %r13, 24(%rdi)     /* save R13 — callee-saved GP register       */
    movq    %r14, 32(%rdi)     /* save R14 — callee-saved GP register       */
    movq    %r15, 40(%rdi)     /* save R15 — callee-saved GP register       */

    /* Save the stack pointer */
    movq    %rsp, 48(%rdi)     /* save RSP — current stack position         */

    /*
     * Save the return address as the instruction pointer.
     * When we later restore this thread, we want to resume right after
     * the call to context_switch_asm(), which is where the return address
     * on the stack points.  We read it from 0(%rsp) — that's where CALL
     * pushed it.
     */
    movq    (%rsp), %rax
    movq    %rax, 56(%rdi)     /* save RIP — return address                 */

    /* ================================================================
     * RESTORE next thread's state from to->th_cpu_state (pointed by RSI)
     * ================================================================ */

    /* Restore callee-saved general-purpose registers */
    movq     0(%rsi), %rbx     /* restore RBX                               */
    movq     8(%rsi), %rbp     /* restore RBP                               */
    movq    16(%rsi), %r12     /* restore R12                               */
    movq    24(%rsi), %r13     /* restore R13                               */
    movq    32(%rsi), %r14     /* restore R14                               */
    movq    40(%rsi), %r15     /* restore R15                               */

    /* Restore the stack pointer */
    movq    48(%rsi), %rsp     /* restore RSP — switches to the new stack   */

    /*
     * Jump to the restored instruction pointer.
     * For a thread that was previously switched out, this returns into
     * the function that called context_switch_asm().
     * For a newly created thread, this jumps to thread_entry_trampoline().
     */
    movq    56(%rsi), %rax
    jmpq    *%rax              /* resume execution at saved RIP              */

    /* NOTREACHED — execution continues in the target thread */
