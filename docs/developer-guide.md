# NEOMACH Developer Guide — C and Assembly Examples

A practical walkthrough of the C and assembly source files in the NEOMACH
kernel for developers joining the project.  Read this alongside the
[architecture overview](architecture.md) and
[build setup guide](build-setup.md).

---

## Table of Contents

1. [Repository Structure](#1-repository-structure)
2. [Build Quick-Start](#2-build-quick-start)
3. [Assembly Examples](#3-assembly-examples)
   - [boot.S — Multiboot entry and 32→64 transition](#31-boots--multiboot-entry-and-3264-transition)
   - [context_switch.S — Low-level context switch](#32-context_switchs--low-level-context-switch)
4. [C Examples](#4-c-examples)
   - [platform/gdt.c — GDT initialisation](#41-platformgdtc--gdt-initialisation)
   - [platform/platform.c — Serial console and I/O helpers](#42-platformplatformc--serial-console-and-io-helpers)
   - [platform/paging.c — 4-level page tables](#43-platformpagingc--4-level-page-tables)
   - [kern/kalloc.c — Bump allocator](#44-kernkallocc--bump-allocator)
   - [kern/klib.c — Freestanding C library](#45-kernklibc--freestanding-c-library)
   - [kern/task.c — Mach task abstraction](#46-kerntaskc--mach-task-abstraction)
   - [kern/thread.c — Mach thread abstraction](#47-kernthreadc--mach-thread-abstraction)
   - [kern/sched.c — Round-robin scheduler](#48-kernschedc--round-robin-scheduler)
   - [ipc/ipc.c — Port and space management](#49-ipciplcc--port-and-space-management)
   - [ipc/ipc_kmsg.c — mach_msg send / receive](#410-ipcipc_kmsgc--mach_msg-send--receive)
   - [kern/kern.c — kernel_main and subsystem init](#411-kernkernc--kernel_main-and-subsystem-init)
5. [Coding Conventions](#5-coding-conventions)
6. [How the Pieces Fit Together](#6-how-the-pieces-fit-together)
7. [Key Patterns to Know](#7-key-patterns-to-know)
8. [Next Steps](#8-next-steps)

---

## 1. Repository Structure

```
neomach/
├── kernel/           NEOMACH Mach microkernel
│   ├── platform/     x86-64 hardware abstraction (boot, GDT, serial, paging)
│   ├── kern/         Kernel core (task, thread, scheduler, allocator)
│   ├── ipc/          Mach IPC (ports, port spaces, message queues)
│   ├── vm/           Virtual memory (page allocator, vm_map)
│   └── include/mach/ Public Mach types and constants
├── servers/          Userspace personality servers (future)
├── docs/             Design docs, RFCs, this guide
├── tools/            QEMU scripts, debug helpers
├── tests/            Kernel test harness
└── cmake/            CMake toolchain files
```

The kernel source is the primary focus of this guide.  Every subsystem
directory contains a `README.md` with design notes specific to that area.

---

## 2. Build Quick-Start

See [docs/build-setup.md](build-setup.md) for the full reference.  The
short version:

```sh
# Prerequisites (macOS)
brew install llvm qemu

# Configure (from the repo root)
cmake -S kernel -B build \
      -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/x86_64-elf-clang.cmake \
      -DNEOMACH_BOOT_TESTS=ON

# Build
cmake --build build

# Run under QEMU (serial output goes to stdout)
./tools/run-qemu.sh --no-build
```

The kernel binary is `build/neomach.elf`.  Press `Ctrl-A X` to quit QEMU.

**Why Clang instead of GCC?**  We target `x86_64-unknown-elf` (a bare-metal
freestanding target).  Clang handles this with a simple `--target=` flag and
ships its own freestanding headers; no separate cross-toolchain installation
is needed.

---

## 3. Assembly Examples

### 3.1 `boot.S` — Multiboot entry and 32→64 transition

**File:** `kernel/platform/boot.S`

This is the very first code the CPU executes.  QEMU's built-in Multiboot
loader hands control to `_start` in 32-bit protected mode with:
- `EAX` = `0x2BADB002` (Multiboot magic)
- `EBX` = physical address of the Multiboot information structure

The file is divided into five clearly-labelled sections:

#### Multiboot1 header

```asm
    .set MB_MAGIC,     0x1BADB002
    .set MB_FLAGS,     0x00010003   /* ALIGN | MEMINFO | AOUT_KLUDGE */
    .set MB_CHECKSUM,  -(MB_MAGIC + MB_FLAGS)
    ...
mb_header:
    .long   MB_MAGIC
    .long   MB_FLAGS
    .long   MB_CHECKSUM
    /* Address fields tell the loader where to copy the raw image */
    .long   mb_header
    .long   mb_header
    .long   __bss_start
    .long   __bss_end
    .long   _start
```

The `AOUT_KLUDGE` flag (`bit 16` of `MB_FLAGS`) adds address fields that
instruct the bootloader to load the kernel as raw bytes, bypassing 64-bit
ELF parsing that QEMU does not support.

#### BSS zero-fill

```asm
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx           /* byte count */
    shrl    $2, %ecx             /* → dword count */
    xorl    %eax, %eax
    rep stosl                    /* zero BSS */
```

This must happen **before** setting up page tables because `boot_pml4`,
`boot_pdpt`, and `boot_pd` live in BSS.  Writing zeros before using them
as page tables prevents a triple fault.

#### Identity-mapped page tables (32-bit)

```asm
    movl    $boot_pdpt, %eax
    orl     $0x03, %eax          /* present + writable */
    movl    %eax, boot_pml4      /* PML4[0] → boot_pdpt */

    movl    $boot_pd, %eax
    orl     $0x03, %eax
    movl    %eax, boot_pdpt      /* PDPT[0] → boot_pd */

    movl    $0x83, %eax          /* present | write | huge (PS=1) */
    movl    %eax, boot_pd        /* PD[0] → first 2 MB */
```

A single 2 MB huge page (`PS` bit set in the PD entry) identity-maps
physical address 0 → virtual address 0 so the CPU can keep executing at
its current physical address after paging is enabled.

#### Long mode activation sequence

```asm
    movl    %cr4, %eax
    orl     $0x20, %eax          /* CR4.PAE = bit 5 */
    movl    %eax, %cr4

    movl    $boot_pml4, %eax
    movl    %eax, %cr3

    movl    $0xC0000080, %ecx    /* EFER MSR */
    rdmsr
    orl     $0x100, %eax         /* EFER.LME = bit 8 */
    wrmsr

    lgdtl   boot_gdt_desc

    movl    %cr0, %eax
    orl     $0x80000001, %eax    /* CR0.PG | CR0.PE */
    movl    %eax, %cr0

    ljmpl   $0x08, $long_mode_entry   /* far jump → reload CS */
```

The sequence follows AMD64 APM §14.6:
1. Enable Physical Address Extension (`CR4.PAE`)
2. Load `CR3` with the PML4 address
3. Set `EFER.LME` (Long Mode Enable)
4. Enable paging + protected mode in `CR0`
5. Far jump to reload `CS` with an L=1 (64-bit) descriptor

#### 64-bit entry

```asm
    .code64
long_mode_entry:
    movw    $0x10, %ax
    movw    %ax, %ds    /* load data selector into all data segment regs */
    ...
    movq    $stack_top, %rsp
    movl    %esi, %edi           /* Multiboot info ptr → RDI (first arg) */
    call    kernel_main
```

After CS is reloaded with a 64-bit descriptor, all data segment registers
are set, a 16 KB stack is established, and `kernel_main()` is called.  The
Multiboot info pointer (saved in ESI in 32-bit mode) is zero-extended into
RDI per the System V AMD64 ABI.

---

### 3.2 `context_switch.S` — Low-level context switch

**File:** `kernel/platform/context_switch.S`

This is the heart of the thread scheduler.  It saves the callee-saved
registers of the outgoing thread and restores those of the incoming thread.

```asm
context_switch_asm:
    /* SAVE — RDI points to from->th_cpu_state */
    movq    %rbx,  0(%rdi)
    movq    %rbp,  8(%rdi)
    movq    %r12, 16(%rdi)
    movq    %r13, 24(%rdi)
    movq    %r14, 32(%rdi)
    movq    %r15, 40(%rdi)
    movq    %rsp, 48(%rdi)
    movq    (%rsp), %rax
    movq    %rax, 56(%rdi)       /* save return address as RIP */

    /* RESTORE — RSI points to to->th_cpu_state */
    movq     0(%rsi), %rbx
    movq     8(%rsi), %rbp
    movq    16(%rsi), %r12
    movq    24(%rsi), %r13
    movq    32(%rsi), %r14
    movq    40(%rsi), %r15
    movq    48(%rsi), %rsp       /* switch stacks */
    movq    56(%rsi), %rax
    jmpq    *%rax                /* jump to saved RIP */
```

**Why only callee-saved registers?**  The System V AMD64 ABI requires that
`RBX`, `RBP`, `R12`–`R15`, and `RSP` survive a function call.  The
caller-saved registers (`RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`–`R11`)
are already saved on the stack by the surrounding C code.

**Why save RIP from `(%rsp)` instead of using `RIP` directly?**  When a
`CALL` instruction executes it pushes the return address onto the stack
before jumping.  At the entry of `context_switch_asm`, `(%rsp)` therefore
holds the return address, which is precisely the instruction we want to
resume at when this thread is next switched back in.

**New thread start:**  For a freshly created thread, `th_cpu_state.rip` is
set to `thread_entry_trampoline` (see `kern/thread.c`) and `r12` holds the
real entry function.  When the context switch restores this state it jumps
to the trampoline, which reads `r12` and calls the entry function.

---

## 4. C Examples

### 4.1 `platform/gdt.c` — GDT initialisation

**File:** `kernel/platform/gdt.c`

The Global Descriptor Table (GDT) must be reloaded with a proper permanent
table early in `platform_init()`.  Boot.S set up a minimal temporary GDT
just for the 32→64 transition; `gdt_init()` replaces it.

```c
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t granularity)
{
    struct gdt_entry *e = &gdt_table[index];
    e->base_low    = (uint16_t)(base & 0xFFFF);
    e->base_middle = (uint8_t)((base >> 16) & 0xFF);
    e->base_high   = (uint8_t)((base >> 24) & 0xFF);
    e->limit_low   = (uint16_t)(limit & 0xFFFF);
    e->granularity = (uint8_t)((granularity & 0xF0) | ((limit >> 16) & 0x0F));
    e->access      = access;
}
```

Three entries are installed: null descriptor (required by the CPU), 64-bit
code segment (`L=1`), and 64-bit data segment.

`gdt_load()` uses inline assembly because:
1. `lgdt` must be called from C to pass a struct pointer.
2. After `lgdt`, `CS` must be reloaded via a far return because you cannot
   encode a 64-bit far jump directly.

```c
static void gdt_load(void)
{
    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq %1\n\t"              /* push code selector */
        "leaq  1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"                 /* far return → reload CS */
        "1:\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        ...
        : : "m"(gdt_desc), "i"((uint64_t)GDT_SELECTOR_CODE),
            "i"((uint16_t)GDT_SELECTOR_DATA) : "rax", "memory"
    );
}
```

---

### 4.2 `platform/platform.c` — Serial console and I/O helpers

**File:** `kernel/platform/platform.c`

Before any graphics or framebuffer support is available, all kernel output
goes to the COM1 serial port (I/O base `0x3F8`).  QEMU redirects this port
to stdout with `-serial stdio`.

The x86 I/O port helpers wrap the `outb`/`inb` instructions:

```c
static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
```

`serial_putchar()` busy-waits until the UART's transmit holding register
is empty (bit 5 of the Line Status Register at `COM1+5`), then writes one
byte:

```c
void serial_putchar(char c)
{
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
        ;
    outb(COM1_PORT, (unsigned char)c);
}
```

`platform_init()` calls `gdt_init()` then `serial_init()`, and is the
first C function called from `long_mode_entry` (via `kernel_main`).

---

### 4.3 `platform/paging.c` — 4-level page tables

**File:** `kernel/platform/paging.c`

`paging_init()` builds the kernel's permanent page tables using statically
allocated arrays (the page allocator is not yet available at this point).

**Virtual address layout (4-level paging):**

```
Bits [47:39] → PML4 index
Bits [38:30] → PDPT index
Bits [29:21] → PD  index
Bits [20:12] → PT  index
Bits [11: 0] → byte offset
```

Two mappings are established using 2 MB huge pages:

```c
/* Identity map: virt 0x00000000 → phys 0x00000000 (first 4 MB) */
identity_pd[0] = 0x00000000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
identity_pd[1] = 0x00200000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
...

/* Higher-half map: virt 0xFFFFFFFF80000000 → phys 0x00000000 */
kernel_pd[0] = 0x00000000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
kernel_pd[1] = 0x00200000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
kernel_pdpt[510] = (uint64_t)kernel_pd  | PTE_PRESENT | PTE_WRITE;
kernel_pml4[511] = (uint64_t)kernel_pdpt | PTE_PRESENT | PTE_WRITE;
```

The identity map (PML4[0]) is kept during Phase 1 to avoid faulting during
the transition.  PML4[511] maps the higher-half kernel window.

The new tables are activated by loading CR3:

```c
__asm__ volatile ("movq %0, %%cr3" : : "r"((uint64_t)kernel_pml4) : "memory");
```

`paging_map()` provides a fine-grained function to map individual 4 KB
pages after the page allocator (`vm_page_alloc`) is available, walking
each level and creating intermediate tables on demand.

---

### 4.4 `kern/kalloc.c` — Bump allocator

**File:** `kernel/kern/kalloc.c`

Phase 1 uses the simplest possible allocator: a bump pointer into a static
heap buffer.

```c
static uint8_t  kheap[KHEAP_SIZE] __attribute__((aligned(16)));
static size_t   kheap_offset;

void *kalloc(size_t size)
{
    size_t aligned_size = ALIGN_UP(size, 16);
    if (kheap_offset + aligned_size > KHEAP_SIZE)
        return (void *)0;
    void *ptr = &kheap[kheap_offset];
    kheap_offset += aligned_size;
    /* zero the allocation */
    for (size_t i = 0; i < aligned_size; i++)
        ((uint8_t *)ptr)[i] = 0;
    return ptr;
}

void kfree(void *ptr) { (void)ptr; /* no-op in Phase 1 */ }
```

All allocations are 16-byte aligned to satisfy the System V AMD64 ABI
requirement for `__m128` types and general safety.  `kfree` is a no-op;
Phase 2 will replace this with a zone-based allocator.

---

### 4.5 `kern/klib.c` — Freestanding C library

**File:** `kernel/kern/klib.c`

The kernel is built freestanding (`-ffreestanding`), so the standard C
library is unavailable.  `klib` provides the minimal set of memory and
string utilities the kernel needs:

| Function | Purpose |
|----------|---------|
| `kmemset` | Fill memory with a byte value |
| `kmemcpy` | Copy memory regions |
| `kmemcmp` | Compare memory regions |
| `kstrlen` | String length |
| `kstrcmp` | String comparison |
| `kstrncpy` | Bounded string copy |

These are simple byte-by-byte implementations with no system calls or
heap dependencies.

---

### 4.6 `kern/task.c` — Mach task abstraction

**File:** `kernel/kern/task.c`

A **task** is the Mach unit of resource ownership.  It holds:
- An IPC port space (`ipc_space`) — the task's capability namespace.
- A virtual memory map (`vm_map`) — the task's address space.
- A list of threads.

```c
struct task *task_create(struct task *parent)
{
    struct task *t = task_pool_alloc();   /* static pool, Phase 1 */
    t->task_id     = next_task_id++;
    t->state       = TASK_STATE_RUNNING;

    /* Each task gets its own port namespace */
    t->t_ipc_space = ipc_space_create(t);

    /* Each task gets its own address space map:
     * start = 0 (low user address), end = higher-half kernel boundary */
    t->t_map = vm_map_create(0, 0xFFFFFFFF80000000ULL);

    return t;
}
```

Phase 1 uses a static pool of `MAX_TASKS` slots rather than dynamic
allocation from a slab, which keeps the code simple while the allocator
matures.

`task_destroy()` tears down the IPC space (dropping all port rights) and
nulls the vm_map pointer.  Full vm_map teardown is a Phase 3 item.

---

### 4.7 `kern/thread.c` — Mach thread abstraction

**File:** `kernel/kern/thread.c`

A **thread** is the unit of execution inside a task.  Creating a thread
sets up its initial CPU state so that the first context switch into the
thread begins execution at the given `entry_point`.

```c
struct thread *thread_create(struct task *task,
                             void (*entry_point)(void),
                             uint32_t stack_size)
{
    struct thread *th = thread_pool_alloc();
    th->th_id    = next_thread_id++;
    th->th_state = THREAD_STATE_RUNNABLE;
    th->th_task  = task;

    void *stack = kalloc(stack_size);
    th->th_stack_top = (uint64_t)stack + stack_size;

    /* Initial CPU state for first context switch */
    th->th_cpu_state.rsp = th->th_stack_top & ~0xFULL;  /* 16-byte align */
    th->th_cpu_state.rsp -= 8;                           /* simulate CALL */
    th->th_cpu_state.rip = (uint64_t)thread_entry_trampoline;
    th->th_cpu_state.r12 = (uint64_t)entry_point;       /* stash real entry */
    ...
}
```

The **trampoline pattern** is worth understanding:

```c
static void thread_entry_trampoline(void)
{
    void (*entry)(void);
    __asm__ volatile ("movq %%r12, %0" : "=r"(entry));
    if (entry) entry();
    for (;;) __asm__ volatile ("hlt");
}
```

Why a trampoline?  `context_switch_asm` jumps (not calls) to the restored
RIP.  If the entry function were placed directly in RIP, there would be no
return address on the stack for when it returns.  The trampoline provides a
safe landing pad that calls the entry function properly and halts cleanly if
it returns.

`thread_switch()` simply updates thread states and delegates to the
assembly `context_switch_asm`:

```c
void thread_switch(struct thread *from, struct thread *to)
{
    from->th_state = THREAD_STATE_RUNNABLE;
    to->th_state   = THREAD_STATE_RUNNING;
    context_switch_asm(&from->th_cpu_state, &to->th_cpu_state);
}
```

---

### 4.8 `kern/sched.c` — Round-robin scheduler

**File:** `kernel/kern/sched.c`

Phase 1 implements cooperative round-robin scheduling with a FIFO run queue
(singly-linked list of `struct thread *`).

```c
static struct thread *run_queue_head;
static struct thread *run_queue_tail;
static struct thread *current_thread;

void sched_enqueue(struct thread *th)
{
    th->th_sched_next = (void *)0;
    if (run_queue_tail)
        run_queue_tail->th_sched_next = th;
    else
        run_queue_head = th;
    run_queue_tail = th;
}

void sched_yield(void)
{
    struct thread *next = sched_dequeue();
    if (!next) return;
    struct thread *prev = current_thread;
    if (prev->th_state != THREAD_STATE_HALTED)
        sched_enqueue(prev);
    current_thread = next;
    thread_switch(prev, next);
}
```

`pit_init()` programs the x86 PIT (Programmable Interval Timer) to fire
IRQ 0 at ~100 Hz using the `outb` I/O port helper:

```c
static void pit_init(void)
{
    uint16_t divisor = 11932;          /* 1193182 Hz / 100 ≈ 11932 (~100 Hz) */
    outb(PIT_COMMAND, 0x34);           /* channel 0, rate generator, binary */
    outb(PIT_CHANNEL0_DATA, divisor & 0xFF);
    outb(PIT_CHANNEL0_DATA, (divisor >> 8) & 0xFF);
}
```

The PIT is configured but IRQ 0 is not yet unmasked in Phase 1.
`sched_tick()` (called when IRQ 0 eventually fires) decrements the current
thread's quantum and calls `sched_yield()` when it expires.

---

### 4.9 `ipc/ipc.c` — Port and space management

**File:** `kernel/ipc/ipc.c`

This file implements the two core Mach IPC data structures:

**`ipc_port`** — a kernel-managed message queue plus metadata.

```c
struct ipc_port *ipc_port_alloc(struct task *receiver)
{
    struct ipc_port *port = kalloc(sizeof(struct ipc_port));
    atomic_init(&port->ip_type, IPC_PORT_TYPE_ACTIVE);
    port->ip_lock        = (atomic_flag)ATOMIC_FLAG_INIT;
    port->ip_send_rights = 0;
    port->ip_receiver    = receiver;
    port->ip_seqno       = 0;
    port->ip_messages    = kalloc(sizeof(struct ipc_mqueue));
    ipc_mqueue_init(port->ip_messages);
    return port;
}
```

**`ipc_space`** — a task's capability namespace (a fixed-size flat table
of `ipc_entry` slots in Phase 1).

```c
struct ipc_space *ipc_space_create(struct task *task)
{
    struct ipc_space *space = kalloc(sizeof(struct ipc_space));
    space->is_active     = 1;
    space->is_task       = task;
    space->is_table_size = IPC_SPACE_MAX_ENTRIES;
    space->is_free_count = IPC_SPACE_MAX_ENTRIES - 1;  /* slot 0 = MACH_PORT_NULL sentinel, never allocated */
    space->is_next_free  = IPC_SPACE_FIRST_VALID;

    for (uint32_t i = 0; i < IPC_SPACE_MAX_ENTRIES; i++) {
        space->is_table[i].ie_object = (void *)0;
        space->is_table[i].ie_bits   = IE_BITS_NONE;
        space->is_table[i].ie_index  = (mach_port_name_t)i;
    }
    return space;
}
```

Port names (the integer values tasks see) are indices into this table.
They are never kernel pointers.  `ipc_space_lookup()` translates a name to
an `ipc_entry`, which holds the kernel `ipc_port *` and the right bits:

```c
struct ipc_entry *ipc_space_lookup(struct ipc_space *space, mach_port_name_t name)
{
    if (name == MACH_PORT_NULL || name >= space->is_table_size)
        return (void *)0;
    struct ipc_entry *e = &space->is_table[name];
    if (ipc_entry_is_free(e))
        return (void *)0;
    return e;
}
```

---

### 4.10 `ipc/ipc_kmsg.c` — `mach_msg` send / receive

**File:** `kernel/ipc/ipc_kmsg.c`

`mach_msg_send()` and `mach_msg_receive()` are the kernel-side
implementations of the fundamental Mach IPC operation.

**Sending:**

```c
kern_return_t mach_msg_send(struct task *sender,
                            mach_msg_header_t *msg, mach_msg_size_t size)
{
    /* 1. Look up destination port name in sender's ipc_space */
    mach_port_name_t dest_name = msg->msgh_remote_port;
    struct ipc_entry *entry = ipc_space_lookup(space, dest_name);

    /* 2. Capability check — sender must hold a SEND or SEND_ONCE right */
    if (!(entry->ie_bits & (IE_BITS_SEND | IE_BITS_SEND_ONCE)))
        return KERN_INVALID_RIGHT;

    /* 3. Check port is alive */
    if (atomic_load(&port->ip_type) == IPC_PORT_TYPE_DEAD)
        return KERN_FAILURE;

    /* 4. Enqueue message (copy semantics in Phase 1) */
    return ipc_mqueue_send(port->ip_messages, msg, size);
}
```

**Receiving:**

```c
kern_return_t mach_msg_receive(struct task *receiver,
                               mach_port_name_t port_name, ...)
{
    /* 1. Look up the port in receiver's space */
    struct ipc_entry *entry = ipc_space_lookup(space, port_name);

    /* 2. Capability check — receiver must hold the RECEIVE right */
    if (!(entry->ie_bits & IE_BITS_RECEIVE))
        return KERN_NOT_RECEIVER;

    /* 3. Dequeue message */
    return ipc_mqueue_receive(port->ip_messages, buf, buf_size, out_size);
}
```

The capability check in step 2 is the cornerstone of Mach's security model.
A task that does not hold the right simply cannot perform the operation —
no separate access-control table is consulted.

---

### 4.11 `kern/kern.c` — `kernel_main` and subsystem init

**File:** `kernel/kern/kern.c`

`kernel_main()` is the C entry point called from `boot.S`.  It initialises
subsystems in strict dependency order:

```c
void kernel_main(void)
{
    serial_putstr("[NEOMACH] kernel_main entered\r\n");

    kalloc_init();   /* 1. heap — everything else needs this */
    ipc_init();      /* 2. IPC port infrastructure */
    vm_init(0, 0);   /* 3. physical page allocator */
    kern_init();     /* 4. kernel task + scheduler */

    create_test_tasks();  /* Phase 1 IPC smoke test */
    bootstrap_main();     /* Phase 1 in-kernel bootstrap demo */

#ifdef NEOMACH_BOOT_TESTS
    ipc_test_run();       /* formal milestone test suite */
#endif

    for (;;) __asm__ volatile ("hlt");   /* halt — preemptive loop in Phase 2 */
}
```

---

## 5. Coding Conventions

### Null pointers

The kernel is built without the C standard library.  `NULL` is not defined,
so null pointers are spelled `(void *)0`:

```c
if (!ptr)
    return (void *)0;
```

### Inline assembly constraints

| Constraint | Meaning |
|-----------|---------|
| `"a"(val)` | Place `val` in `RAX`/`EAX`/`AL` |
| `"Nd"(port)` | Port number in `RDX` (or as 8-bit immediate if ≤ 255) |
| `"r"(val)` | Any general-purpose register |
| `"m"(mem)` | Memory operand |
| `"=r"(out)` | Write result to any GP register |
| `"memory"` | Clobber: compiler may not cache memory across this |
| `"volatile"` | Prevent the compiler from moving or eliminating the asm |

### C11 atomics for lock-free checks

Port state (`ip_type`) uses C11 `_Atomic` so that concurrent senders can
do a cheap lock-free dead-port check before taking `ip_lock`:

```c
atomic_store_explicit(&port->ip_type, IPC_PORT_TYPE_DEAD,
                      memory_order_release);
...
int type = atomic_load_explicit(&port->ip_type, memory_order_acquire);
```

### Static pools vs. dynamic allocation

Phase 1 uses fixed-size static arrays for tasks and threads to avoid
depending on a mature allocator.  When you see `static struct task
task_pool[MAX_TASKS]`, that is an intentional Phase 1 simplification, not
a final design.

### Function comment style

Every public function has a short comment above it (or a `/* --- */`
section banner) explaining its purpose.  Implementation details are
commented inline where non-obvious.

---

## 6. How the Pieces Fit Together

```
                boot.S (_start)
                   │
                   │  zero BSS, build page tables,
                   │  enable long mode, far jump to 64-bit
                   ▼
           long_mode_entry
                   │  set up stack, call kernel_main
                   ▼
             kernel_main()          kern/kern.c
                   │
       ┌───────────┼───────────┐
       ▼           ▼           ▼
  kalloc_init   ipc_init    vm_init
  (kern/)       (ipc/)      (vm/)
                             │
                             ▼
                         kern_init()
                             │
                   ┌─────────┴──────────┐
                   ▼                    ▼
             kernel_task_init       sched_init
             (creates task 0)       (PIT setup,
              + ipc_space            run queue)
                   │
                   ▼
            create_test_tasks()     IPC smoke test
                   │
                   ▼
            bootstrap_main()        service registry demo
```

**Context switch path:**

```
sched_yield()
    └─ thread_switch(prev, next)      kern/thread.c
           └─ context_switch_asm()   platform/context_switch.S
                  saves prev's RBX/RBP/R12-R15/RSP/RIP
                  restores next's registers
                  jmpq to next's RIP
```

**IPC send path:**

```
mach_msg_send(sender, msg, size)      ipc/ipc_kmsg.c
    ├─ ipc_space_lookup(space, name)  ipc/ipc.c
    ├─ check IE_BITS_SEND             capability gate
    └─ ipc_mqueue_send(queue, msg)    ipc/ipc_mqueue.c
           └─ kalloc(size)            copy into kernel buffer
```

---

## 7. Key Patterns to Know

### Acquire the lock, look up, release early

```c
ipc_space_lock(space);
struct ipc_entry *entry = ipc_space_lookup(space, name);
if (!entry) {
    ipc_space_unlock(space);
    return KERN_INVALID_NAME;
}
struct ipc_port *port = entry->ie_object;
ipc_space_unlock(space);          /* ← release before touching port */
```

The space lock is released before the port lock is acquired to avoid
lock-order inversions.

### Guard with `if (!ptr) return (void *)0`

Every allocation result is checked immediately.  The kernel must never
dereference a null pointer — there is no memory protection handler to
catch it gracefully in early boot.

### TODO markers signal Phase boundaries

```c
/* TODO (Phase 2): Implement zone-based allocation with real freeing. */
```

Search for `TODO` comments to find work items for the next phase.

### Inline assembly for hardware access

Direct hardware access (I/O ports, CR registers, MSRs) uses GCC/Clang
extended inline assembly.  Look at `platform/platform.c` (`outb`/`inb`)
and `platform/gdt.c` (`lgdt`, `lretq`) for representative examples.

---

## 8. Next Steps

Once you are comfortable with the examples above, the best places to
contribute are marked with `TODO (Phase 2)` comments:

- **`kern/kalloc.c`** — replace the bump allocator with a zone-based
  allocator that supports `kfree`.
- **`kern/sched.c`** — wire up the IDT, unmask IRQ 0 from the PIC, and
  enable preemptive scheduling via `sched_tick`.
- **`ipc/ipc.c`** — drain the message queue on port destruction; walk all
  spaces that hold send rights and convert them to dead names.
- **`platform/paging.c`** — remove the Phase 1 identity map after
  completing the switch to higher-half virtual addresses.
- **`kern/thread.c`** — implement `thread_destroy` with proper stack
  freeing and scheduler removal.

See [TASKS.md](../TASKS.md) for the full prioritised task list and
[docs/roadmap.md](roadmap.md) for the development roadmap.
