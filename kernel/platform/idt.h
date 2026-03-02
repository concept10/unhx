/*
 * kernel/platform/idt.h — Interrupt Descriptor Table for UNHOX (x86-64)
 *
 * The IDT maps interrupt/exception vectors (0–255) to handler routines.
 * x86-64 uses 16-byte IDT entries ("interrupt gate descriptors") that
 * contain a 64-bit handler address and the target code segment selector.
 *
 * Vectors 0–31:   CPU exceptions (divide error, page fault, GPF, etc.)
 * Vectors 32–47:  Hardware IRQs (remapped via the i8259 PIC)
 * Vector 128:     System call trap gate (int 0x80, DPL=3)
 * Vectors 48–255: Reserved / unused
 *
 * Reference: Intel SDM Vol. 3A §6.14 — 64-Bit IDT Gate Descriptors;
 *            AMD64 APM Vol. 2 §8.2 — Interrupt-Gate and Trap-Gate Descriptors.
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Total number of IDT entries */
#define IDT_ENTRIES     256

/* PIC-remapped IRQ base vector */
#define IRQ_BASE        0x20

/* Convenience: IRQ number → IDT vector */
#define IRQ_VECTOR(n)   (IRQ_BASE + (n))

/* System call vector */
#define SYSCALL_VECTOR  0x80

/*
 * struct idt_entry — one 16-byte 64-bit IDT gate descriptor.
 *
 * Layout (Intel SDM §6.14.1):
 *   [0:15]   offset_low   — bits 0–15 of handler address
 *   [16:31]  selector     — code segment selector (must be a 64-bit CS)
 *   [32:34]  ist          — Interrupt Stack Table index (0 = no IST)
 *   [35:39]  reserved     — must be zero
 *   [40:43]  type         — gate type (0xE = interrupt gate, 0xF = trap gate)
 *   [44]     zero         — must be zero
 *   [45:46]  dpl          — descriptor privilege level
 *   [47]     present      — present bit
 *   [48:63]  offset_mid   — bits 16–31 of handler address
 *   [64:95]  offset_high  — bits 32–63 of handler address
 *   [96:127] reserved     — must be zero
 */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;           /* bits [2:0] = IST index, rest reserved */
    uint8_t  type_attr;     /* P(1) DPL(2) 0(1) Type(4) */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/* Gate type constants */
#define IDT_GATE_INTERRUPT  0x8E    /* P=1, DPL=0, type=0xE (interrupt gate) */
#define IDT_GATE_TRAP       0x8F    /* P=1, DPL=0, type=0xF (trap gate)      */
#define IDT_GATE_USER_TRAP  0xEF    /* P=1, DPL=3, type=0xF (user-callable)  */

/* IDTR — loaded by lidt */
struct idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/*
 * Interrupt frame — pushed by the CPU and our ISR stubs.
 *
 * When an interrupt/exception fires, the CPU pushes (on the kernel stack):
 *   SS, RSP, RFLAGS, CS, RIP, [error_code]
 * Our ISR stub then pushes a dummy error code (if needed), the vector number,
 * and all general-purpose registers.
 */
struct interrupt_frame {
    /* Pushed by ISR stub (pushaq macro) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* Pushed by ISR stub */
    uint64_t vector;
    uint64_t error_code;
    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/*
 * idt_init — build the IDT and load it with lidt.
 * Installs handlers for all 256 vectors.
 * Must be called after gdt_init() (needs valid code segment selector).
 */
void idt_init(void);

/*
 * idt_set_gate — install a handler for a specific vector.
 * Used internally and by the syscall mechanism (Step 9) to install
 * a DPL=3 gate for int 0x80.
 */
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector,
                  uint8_t type_attr);

#endif /* IDT_H */
