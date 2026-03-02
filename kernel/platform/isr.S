/*
 * kernel/platform/isr.S — Interrupt Service Routine stubs for UNHOX (x86-64)
 *
 * Provides 256 ISR entry points (isr_stub_0 through isr_stub_255).
 * Each stub:
 *   1. Pushes a dummy error code (0) if the CPU didn't push one
 *   2. Pushes the vector number
 *   3. Saves all general-purpose registers
 *   4. Calls the C handler (isr_dispatch)
 *   5. Restores registers and returns via iretq
 *
 * The C handler receives a pointer to struct interrupt_frame (defined in idt.h).
 *
 * Reference: Intel SDM Vol. 3A §6.12 — Exception and Interrupt Handling;
 *            AMD64 APM Vol. 2 §8.9 — Long-Mode Interrupt Control Transfers.
 */

    .text
    .code64

/*
 * Save all general-purpose registers.
 * Must match struct interrupt_frame layout (r15 first, rax last).
 */
.macro pushaq
    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
.endm

/* Restore all general-purpose registers (reverse order of pushaq). */
.macro popaq
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax
.endm

/* =========================================================================
 * Common ISR handler — all stubs jump here after pushing vector + error_code.
 * ========================================================================= */

isr_common:
    pushaq
    movq    %rsp, %rdi          /* arg0 = pointer to interrupt_frame */
    call    isr_dispatch
    popaq
    addq    $16, %rsp           /* pop vector + error_code */
    iretq

/* =========================================================================
 * ISR stub macros
 *
 * ISR_ERR:   for exceptions that push an error code (8, 10–14, 17, 21, 29, 30)
 * ISR_NOERR: for all others (pushes dummy error code 0)
 * ========================================================================= */

.macro ISR_ERR vector
    .globl isr_stub_\vector
    .type  isr_stub_\vector, @function
    isr_stub_\vector:
        pushq   $\vector
        jmp     isr_common
.endm

.macro ISR_NOERR vector
    .globl isr_stub_\vector
    .type  isr_stub_\vector, @function
    isr_stub_\vector:
        pushq   $0              /* dummy error code */
        pushq   $\vector
        jmp     isr_common
.endm

/* =========================================================================
 * CPU Exceptions (vectors 0–31)
 *
 * Vectors that push an error code: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
 * All others: no error code.
 * ========================================================================= */

ISR_NOERR 0     /* #DE  Divide Error */
ISR_NOERR 1     /* #DB  Debug Exception */
ISR_NOERR 2     /* NMI  Non-Maskable Interrupt */
ISR_NOERR 3     /* #BP  Breakpoint */
ISR_NOERR 4     /* #OF  Overflow */
ISR_NOERR 5     /* #BR  Bound Range Exceeded */
ISR_NOERR 6     /* #UD  Invalid Opcode */
ISR_NOERR 7     /* #NM  Device Not Available */
ISR_ERR   8     /* #DF  Double Fault */
ISR_NOERR 9     /* ---  Coprocessor Segment Overrun (reserved) */
ISR_ERR   10    /* #TS  Invalid TSS */
ISR_ERR   11    /* #NP  Segment Not Present */
ISR_ERR   12    /* #SS  Stack-Segment Fault */
ISR_ERR   13    /* #GP  General Protection Fault */
ISR_ERR   14    /* #PF  Page Fault */
ISR_NOERR 15    /* ---  Reserved */
ISR_NOERR 16    /* #MF  x87 Floating-Point Exception */
ISR_ERR   17    /* #AC  Alignment Check */
ISR_NOERR 18    /* #MC  Machine Check */
ISR_NOERR 19    /* #XM  SIMD Floating-Point Exception */
ISR_NOERR 20    /* #VE  Virtualization Exception */
ISR_ERR   21    /* #CP  Control Protection Exception */
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29    /* #HV  Hypervisor Injection Exception */
ISR_ERR   30    /* #SX  Security Exception */
ISR_NOERR 31

/* =========================================================================
 * Hardware IRQs (vectors 32–47) and remaining vectors (48–255).
 * None of these push error codes.
 *
 * Generated with altmacro to allow numeric substitution in symbol names.
 * ========================================================================= */

.altmacro

.macro GEN_ISR_NOERR n
    ISR_NOERR n
.endm

.set i, 32
.rept 224
    GEN_ISR_NOERR %i
    .set i, i + 1
.endr

/* =========================================================================
 * ISR stub table — array of 256 function pointers.
 * Used by idt_init() in idt.c to populate the IDT.
 * ========================================================================= */

.macro STUB_ENTRY n
    .quad isr_stub_\n
.endm

    .section .rodata
    .align 8
    .globl isr_stub_table
    .type  isr_stub_table, @object
isr_stub_table:
.set i, 0
.rept 256
    STUB_ENTRY %i
    .set i, i + 1
.endr
