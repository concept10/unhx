/*
 * kernel/platform/context_switch.S — x86-64 context switch for UNHOX
 *
 * Saves the callee-saved registers of the current thread onto its stack,
 * switches the stack pointer, and restores the next thread's registers.
 *
 * System V AMD64 ABI callee-saved registers:
 *   RBX, RBP, R12, R13, R14, R15
 *   RSP — stack pointer (explicitly saved/restored)
 *
 * The push/pop approach ensures the stack is balanced: `ret` pops the
 * return address from the stack, so resumed threads unwind correctly.
 * For new threads, thread_create() pre-builds a matching stack frame
 * with the trampoline address as the return address.
 *
 * Parameters (System V AMD64 ABI calling convention):
 *   RDI = pointer to from_thread->th_cpu_state (struct cpu_state *)
 *   RSI = pointer to to_thread->th_cpu_state   (struct cpu_state *)
 *
 * Only th_cpu_state.rsp (offset 48) is used by this function.
 *
 * Reference: System V AMD64 ABI §3.2 — Function Calling Convention;
 *            AMD64 Architecture Programmer's Manual Vol. 1 §3.3.
 */

    .section .text
    .globl  context_switch_asm
    .type   context_switch_asm, @function

context_switch_asm:
    /* ================================================================
     * SAVE current thread's callee-saved registers onto its stack.
     *
     * The call instruction that brought us here already pushed the
     * return address.  We push the 6 callee-saved GPRs on top of it.
     * ================================================================ */

    pushq   %rbx
    pushq   %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    /* Save the current stack pointer into from->th_cpu_state.rsp */
    movq    %rsp, 48(%rdi)

    /* ================================================================
     * RESTORE next thread's state.
     *
     * Switch to the target thread's stack, pop its callee-saved
     * registers, and `ret` to its saved return address.
     *
     * For a previously running thread, ret returns to the instruction
     * after `call context_switch_asm` in thread_switch().
     *
     * For a newly created thread, ret jumps to thread_entry_trampoline
     * (whose address was placed on the stack by thread_create()).
     * ================================================================ */

    movq    48(%rsi), %rsp

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    popq    %rbx

    ret

    /* NOTREACHED — execution continues in the target thread */

/* ====================================================================
 * thread_entry_trampoline — entry point for newly created threads.
 *
 * When context_switch_asm `ret`s into a new thread for the first time,
 * R12 contains the real entry point (set up by thread_create on the
 * initial stack frame and popped by context_switch_asm).
 *
 * We must read R12 in assembly BEFORE any C compiler-generated code
 * runs, because the compiler may freely clobber callee-saved registers
 * in a function prologue.
 *
 * This trampoline:
 *   1. Enables interrupts (the context switch occurred inside a timer
 *      ISR where IF was cleared by the interrupt gate).
 *   2. Passes the entry point to thread_entry_helper() in RDI.
 *   3. thread_entry_helper() calls the real entry function.
 * ==================================================================== */

    .globl  thread_entry_trampoline
    .type   thread_entry_trampoline, @function
thread_entry_trampoline:
    sti                     /* enable interrupts for this thread         */
    movq    %r12, %rdi      /* pass entry_point as first C argument      */
    call    thread_entry_helper

    /* thread_entry_helper should never return, but just in case: */
.Ltrampoline_hlt:
    cli
    hlt
    jmp     .Ltrampoline_hlt
