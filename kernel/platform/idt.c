/*
 * kernel/platform/idt.c — Interrupt Descriptor Table for UNHOX (x86-64)
 *
 * Builds a 256-entry IDT, installs CPU exception handlers, and loads
 * the IDTR via lidt.
 *
 * Reference: Intel SDM Vol. 3A §6.14 — 64-Bit IDT Gate Descriptors.
 */

#include "idt.h"
#include "pic.h"
#include "gdt.h"
#include "vm/vm_fault.h"

/* Serial output (platform layer) */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/* ISR stub table — defined in isr.S */
extern uint64_t isr_stub_table[IDT_ENTRIES];

/* The IDT itself — 256 entries × 16 bytes = 4096 bytes (one page) */
static struct idt_entry idt_table[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_descriptor idt_desc;

/* Exception name table for diagnostic messages */
static const char *exception_names[32] = {
    "Divide Error (#DE)",
    "Debug (#DB)",
    "Non-Maskable Interrupt (NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 FP Exception (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD FP Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection (#HV)",
    "Security Exception (#SX)",
    "Reserved"
};

void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector,
                  uint8_t type_attr)
{
    struct idt_entry *e = &idt_table[vector];

    e->offset_low  = (uint16_t)(handler & 0xFFFF);
    e->offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    e->offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    e->selector    = selector;
    e->ist         = 0;
    e->type_attr   = type_attr;
    e->reserved    = 0;
}

/* Read CR2 (faulting address for page faults) */
static inline uint64_t read_cr2(void)
{
    uint64_t val;
    __asm__ volatile ("movq %%cr2, %0" : "=r"(val));
    return val;
}

/*
 * isr_dispatch — C-level interrupt/exception handler.
 *
 * Called from the assembly stubs in isr.S with a pointer to the
 * interrupt frame on the kernel stack.
 */
void isr_dispatch(struct interrupt_frame *frame)
{
    uint64_t vec = frame->vector;

    if (vec < 32) {
        /* CPU exception */
        serial_putstr("\r\n!!! EXCEPTION: ");
        serial_putstr(exception_names[vec]);
        serial_putstr("\r\n");

        serial_putstr("  Vector:     ");
        serial_putdec((uint32_t)vec);
        serial_putstr("\r\n");

        serial_putstr("  Error code: ");
        serial_puthex(frame->error_code);
        serial_putstr("\r\n");

        serial_putstr("  RIP:        ");
        serial_puthex(frame->rip);
        serial_putstr("\r\n");

        serial_putstr("  CS:         ");
        serial_puthex(frame->cs);
        serial_putstr("\r\n");

        serial_putstr("  RFLAGS:     ");
        serial_puthex(frame->rflags);
        serial_putstr("\r\n");

        serial_putstr("  RSP:        ");
        serial_puthex(frame->rsp);
        serial_putstr("\r\n");

        if (vec == 14) {
            /* Page fault: attempt VM recovery before panicking. */
            uint64_t fault_addr = read_cr2();
            kern_return_t kr = vm_fault_handle(frame, fault_addr, frame->error_code);
            if (kr == KERN_SUCCESS)
                return;

            serial_putstr("  CR2 (fault): ");
            serial_puthex(fault_addr);
            serial_putstr("\r\n");

            serial_putstr("  vm_fault:   ");
            serial_puthex((uint64_t)(uint32_t)kr);
            serial_putstr("\r\n");
        }

        serial_putstr("  RAX:        ");
        serial_puthex(frame->rax);
        serial_putstr("\r\n");

        serial_putstr("  RBX:        ");
        serial_puthex(frame->rbx);
        serial_putstr("\r\n");

        serial_putstr("  RCX:        ");
        serial_puthex(frame->rcx);
        serial_putstr("\r\n");

        serial_putstr("  RDX:        ");
        serial_puthex(frame->rdx);
        serial_putstr("\r\n");

        /* Halt on unrecoverable exceptions */
        serial_putstr("!!! HALTING\r\n");
        for (;;)
            __asm__ volatile ("cli; hlt");
    }

    if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
        /* Hardware IRQ */
        uint8_t irq = (uint8_t)(vec - IRQ_BASE);

        /*
         * Send EOI BEFORE calling the handler.  If the handler triggers
         * a context switch (e.g. sched_tick → sched_yield), we never
         * return here, so the EOI must already be sent.  This is safe
         * because interrupts are disabled (interrupt gate clears IF),
         * preventing nested interrupts.
         */
        pic_eoi(irq);

        extern void irq_handler(uint8_t irq, struct interrupt_frame *frame);
        irq_handler(irq, frame);
        return;
    }

    /* System call: int 0x80 */
    if (vec == SYSCALL_VECTOR) {
        extern void syscall_dispatch(struct interrupt_frame *frame);
        syscall_dispatch(frame);
        return;
    }

    /* Unhandled vector — just return */
}

void idt_init(void)
{
    /* Install all 256 ISR stubs as interrupt gates (DPL=0) */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i],
                     GDT_SELECTOR_CODE, IDT_GATE_INTERRUPT);
    }

    /*
     * Vector 0x80 — system call trap gate (DPL=3).
     * User-mode code can trigger this via `int $0x80`.
     * It is a trap gate (not interrupt gate) so interrupts remain enabled
     * during syscall processing.
     */
    idt_set_gate(SYSCALL_VECTOR, isr_stub_table[SYSCALL_VECTOR],
                 GDT_SELECTOR_CODE, IDT_GATE_USER_TRAP);

    /* Load the IDTR */
    idt_desc.limit = (uint16_t)(sizeof(idt_table) - 1);
    idt_desc.base  = (uint64_t)(uintptr_t)idt_table;

    __asm__ volatile ("lidt %0" : : "m"(idt_desc));
}
