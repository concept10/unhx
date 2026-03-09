# NEOMACH Kernel — Use Cases, Platform Targets, and Limits

> **Document scope:** This document comprehensively describes the deployment scenarios the NEOMACH
> Mach microkernel is designed for, the processor and memory constraints it imposes, and concrete
> examples of platforms — from embedded edge nodes to industrial PLCs to traditional desktops —
> that are realistic targets.  It also honestly enumerates the current limits and what would need
> to change before each new domain becomes viable.

---

## Table of Contents

1. [Why a Mach Microkernel?](#1-why-a-mach-microkernel)
2. [The Four Kernel Invariants](#2-the-four-kernel-invariants)
3. [Deployment Domains](#3-deployment-domains)
   - 3.1 [General-Purpose Desktop / Workstation](#31-general-purpose-desktop--workstation)
   - 3.2 [Server / Cloud Instance](#32-server--cloud-instance)
   - 3.3 [Embedded Systems](#33-embedded-systems)
   - 3.4 [Edge Devices and IoT Gateways](#34-edge-devices-and-iot-gateways)
   - 3.5 [Real-Time Systems (RTOS Profile)](#35-real-time-systems-rtos-profile)
   - 3.6 [Programmable Logic Controller (PLC) Runtime](#36-programmable-logic-controller-plc-runtime)
   - 3.7 [Virtual PLC Runtime](#37-virtual-plc-runtime)
   - 3.8 [Hypervisor / Type-1 VMM](#38-hypervisor--type-1-vmm)
4. [Processor Architecture Requirements](#4-processor-architecture-requirements)
   - 4.1 [Currently Supported Architectures](#41-currently-supported-architectures)
   - 4.2 [Minimum Hardware Requirements](#42-minimum-hardware-requirements)
   - 4.3 [Porting to New Architectures](#43-porting-to-new-architectures)
5. [Memory Model Limits](#5-memory-model-limits)
6. [IPC Throughput Limits](#6-ipc-throughput-limits)
7. [Scheduling Limits](#7-scheduling-limits)
8. [What the Kernel Deliberately Does NOT Do](#8-what-the-kernel-deliberately-does-not-do)
9. [Decision Guide: Is NEOMACH Right for My Project?](#9-decision-guide-is-neomach-right-for-my-project)
10. [Platform Examples](#10-platform-examples)
    - 10.1 [x86-64 Desktop / Workstation](#101-x86-64-desktop--workstation)
    - 10.2 [ARM Cortex-A (Raspberry Pi, BeagleBone, NXP i.MX)](#102-arm-cortex-a-raspberry-pi-beaglebone-nxp-imx)
    - 10.3 [ARM Cortex-M (Microcontrollers — Exploratory)](#103-arm-cortex-m-microcontrollers--exploratory)
    - 10.4 [RISC-V (SiFive, StarFive, Microchip PolarFire)](#104-risc-v-sifive-starfive-microchip-polarfire)
    - 10.5 [FPGA Soft-Core CPU (Xilinx MicroBlaze, Lattice RISC-V)](#105-fpga-soft-core-cpu-xilinx-microblaze-lattice-risc-v)
    - 10.6 [Industrial PC (IPC) / Beckhoff CX / Siemens IPC](#106-industrial-pc-ipc--beckhoff-cx--siemens-ipc)
    - 10.7 [Automotive-Grade SoC (NXP S32G, Renesas R-Car)](#107-automotive-grade-soc-nxp-s32g-renesas-r-car)
11. [Comparison with Other Approaches](#11-comparison-with-other-approaches)
12. [Roadmap for Non-Desktop Targets](#12-roadmap-for-non-desktop-targets)
13. [References](#13-references)

---

## 1. Why a Mach Microkernel?

Traditional monolithic kernels (Linux, FreeBSD) couple all OS services — scheduling, filesystems,
networking, device drivers — into a single privileged binary.  A fault in any driver crashes the
whole system.  A security vulnerability in any driver becomes a kernel-level exploit.

NEOMACH follows the **Mach microkernel** discipline: the kernel provides only four primitives, and
everything else runs as ordinary (fault-isolated) userspace processes communicating over Mach
ports.  This makes the kernel:

| Property | Consequence |
|----------|-------------|
| **Tiny TCB (Trusted Computing Base)** | Fewer than ~30 KLOC in the kernel proper; vastly smaller attack surface than Linux (~30 MLOC) |
| **Fault-isolated services** | A crashing filesystem server is restarted; the kernel stays up |
| **Capability-based security** | Holding a port send-right *is* the permission; no ambient authority |
| **Portable core** | Platform code is <5% of the kernel; the rest is architecture-independent C |
| **Configurable personality** | Run BSD servers for POSIX, run IEC 61131-3 servers for PLC I/O, run AUTOSAR servers for automotive — same kernel binary, different servers |

These properties make NEOMACH an unusually good fit for **safety-critical, mixed-criticality, and
resource-constrained** environments, *not just* desktop use.

---

## 2. The Four Kernel Invariants

Regardless of deployment domain, the NEOMACH kernel exposes exactly four primitives:

```
┌─────────────────────────────────────────────────────────┐
│  IPC       Mach ports — the sole cross-domain channel   │
│  VM        Virtual memory — maps, objects, pagers       │
│  Tasks     Address-space + port-space containers        │
│  Threads   Units of CPU execution; scheduled by kernel  │
└─────────────────────────────────────────────────────────┘
```

Everything above this line is a **server** — a userspace program communicating through IPC.
This means the kernel binary is the *same* whether NEOMACH is running a full NeXT-heritage desktop
or a headless real-time PLC runtime.  Only the servers launched at boot differ.

---

## 3. Deployment Domains

### 3.1 General-Purpose Desktop / Workstation

**Status:** Primary development target (Phase 1–5 roadmap).

The NEOMACH full stack (BSD server + VFS + networking + NeXT/GNUstep frameworks) runs on x86-64
and AArch64 workstations as a complete NeXT-heritage desktop OS.  This is the scenario described
in the main [README](../README.md) and [roadmap](roadmap.md).

**Realistic use:** Research OS, software-archaeology exploration of the NeXT/OpenStep lineage,
teaching OS design.

**Prerequisites on hardware:**
- 64-bit CPU with hardware MMU and a supported ISA (see §4)
- ≥ 256 MB RAM (kernel + all servers + frameworks)
- UEFI or Multiboot2 bootloader

---

### 3.2 Server / Cloud Instance

**Status:** Realistic after Phase 2 (BSD server + networking).

Because every NEOMACH personality server is an ordinary userspace process, it is straightforward to
build a minimal "headless" server image: kernel + BSD server + network server + VFS server, without
any display or desktop components.

**Realistic use:**
- Research microkernel-based application servers
- Capability-based cloud-native services where port rights replace ambient credentials
- Secure multi-tenant compute (each tenant's workload in an isolated task set)

**Cloud example:**
```
NEOMACH VM (qemu/kvm guest)
  ├── kernel (Mach)
  ├── bootstrap server
  ├── network server  ← virtio-net driver
  ├── vfs server      ← virtio-blk driver + ext2
  ├── bsd server      ← POSIX personality
  └── application tasks (your microservice)
```

---

### 3.3 Embedded Systems

**Status:** Kernel architecture is compatible; embedded profile server set is future work.

Mach was originally designed to run on machines with 4–8 MB of RAM.  NEOMACH inherits that DNA.
A minimal NEOMACH kernel — IPC + minimal VM + task/thread — fits comfortably on systems with:

- **32 MB RAM** (kernel only, no BSD server)
- **64 MB RAM** (kernel + minimal device + bootstrap servers)
- **128 MB RAM** (kernel + device + network + a single-purpose application server)

For embedded targets, the NeXT/GNUstep desktop stack is *not* loaded.  Instead, domain-specific
servers replace it:

```
┌──────────────────────────────────────────────────────┐
│  Application Server (your code, e.g. sensor logger)  │
├──────────────────────────────────────────────────────┤
│  Device Server (GPIO, SPI, I2C, UART, CAN)           │
│  Network Server (lwIP / embedded TCP stack)           │
├──────────────────────────────────────────────────────┤
│  Bootstrap Server                                     │
├──────────────────────────────────────────────────────┤
│  MACH MICROKERNEL  (IPC · VM · Tasks · Threads)      │
├──────────────────────────────────────────────────────┤
│  Hardware Abstraction  (AArch64 / RISC-V / x86)      │
└──────────────────────────────────────────────────────┘
```

**Porting prerequisites for a new embedded platform:**
1. Implement `kernel/platform/<arch>/boot.S` — early boot, stack setup
2. Implement `kernel/platform/<arch>/platform.c` — UART console, timer
3. Implement `kernel/platform/<arch>/pmap.c` — MMU page table management
4. Implement `kernel/platform/<arch>/context_switch.S` — thread context switch

The AArch64 platform layer is the farthest ahead and serves as the reference template for new
ports (see `kernel/platform/aarch64/`).

---

### 3.4 Edge Devices and IoT Gateways

**Status:** Realistic after Phase 2; AArch64 port enables this path.

Edge computing places computation close to data sources: industrial sensors, smart cameras,
smart meters, agricultural monitoring.  NEOMACH's properties are well-suited:

| NEOMACH Property | Edge Benefit |
|----------------|-------------|
| Fault isolation | A crashing sensor-driver server cannot corrupt the aggregation pipeline |
| Port-based capabilities | Fine-grained, auditable access control for each data stream |
| Small kernel footprint | Fits alongside an RTOS on a heterogeneous SoC (see §3.5) |
| Hot-replaceable servers | Over-the-air update of a single server without rebooting |

**Typical IoT/edge topology:**
```
  [Sensor A]──────┐
  [Sensor B]──────┤   ┌──────────────────────────────────────┐
  [Sensor C]──────┴──►│  Edge Gateway (ARM Cortex-A, RISC-V) │
                       │  ┌──────────────────────────────────┐│
                       │  │ NEOMACH                            ││
                       │  │  sensor-collector server         ││
                       │  │  pre-processing server           ││
                       │  │  MQTT/CoAP network server        ││
                       │  │  secure-storage server           ││
                       │  │  ── Mach kernel ──               ││
                       │  └──────────────────────────────────┘│
                       └──────────────────────────────────────┘
                                         │ (TLS to cloud)
                                         ▼
                                    Cloud backend
```

**Processor examples:** Raspberry Pi 4/5 (Cortex-A72/A76), NVIDIA Jetson Orin
(Cortex-A78AE), NXP i.MX 8M Plus, BeagleBone AI-64 (TDA4VM), StarFive JH7110.

---

### 3.5 Real-Time Systems (RTOS Profile)

**Status:** Architectural design required; kernel changes needed.

Mach was not originally a hard-RTOS kernel, but the OSF MK series added real-time extensions
(MK++ / CHORUS MiX) that gave bounded interrupt latency and priority-ceiling mutexes.  NEOMACH
can follow that path.

#### What is needed for hard real-time?

| Requirement | NEOMACH Status | Work Required |
|-------------|-------------|---------------|
| Bounded interrupt latency | Not yet guaranteed | Disable preemption windows, audit all spinlock sections |
| Priority-based preemptive scheduling | Phase 2 (basic priority) | Add priority-ceiling / priority-inheritance |
| Rate-monotonic / EDF scheduling | Not planned | Add scheduling policy server |
| Deterministic IPC latency | Phase 1 uses copy semantics | Add zero-copy fast path for small messages |
| Real-time clock / timer resolution | Platform-specific | Platform timer drivers with sub-millisecond resolution |
| Memory locking (no page faults on critical path) | Not yet | `vm_wire()` / `mlock()` equivalent |

#### Realistic RTOS profile

A realistic "soft real-time" NEOMACH profile (millisecond-order latency) is achievable with
Phase 2 work.  Hard real-time (microsecond-order, POSIX `SCHED_FIFO`-equivalent) requires the
additional work listed above.

**Approach — Asymmetric Multiprocessing (AMP):**
On multi-core SoCs (Cortex-A + Cortex-M heterogeneous, or multi-core A-class with core
partitioning), NEOMACH can dedicate one core to a hard-RT domain and run the general-purpose
Mach stack on the remaining cores.  Mach IPC over shared memory provides the cross-domain
channel.  This mirrors the approach used by QNX Neutrino on NXP i.MX and the AUTOSAR CP/AP
split.

```
Core 0 (Hard RT)          Cores 1–N (NEOMACH Mach)
┌────────────────┐         ┌────────────────────────────┐
│ RT Task        │  Mach   │ Mach kernel                │
│ (deterministic │◄──IPC──►│ device server              │
│  control loop) │ shared  │ app servers                │
└────────────────┘ memory  └────────────────────────────┘
```

---

### 3.6 Programmable Logic Controller (PLC) Runtime

**Status:** Concept / future research target.

A PLC runtime executes IEC 61131-3 programs (Ladder Diagram, Structured Text, Function Block
Diagram, etc.) in a deterministic scan cycle against physical I/O.

NEOMACH's capability-based isolation model maps naturally to PLC concepts:

| PLC Concept | NEOMACH Equivalent |
|-------------|-----------------|
| I/O module | Device server holding SEND rights to hardware I/O ports |
| PLC program task | Mach task with SEND rights only to permitted I/O ports |
| Program-to-program messaging | Mach IPC messages |
| Watchdog / task supervision | Kernel task-death notifications; supervisor server |
| Deterministic scan cycle | Real-time scheduling policy (see §3.5) + `vm_wire` |

#### Envisioned architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│  IEC 61131-3 PLC Runtime (NEOMACH target profile)                      │
├─────────────────────────────────────────────┬────────────────────────┤
│  PLC Program Task 1 (e.g. safety interlock) │  PLC Program Task 2    │
│  (holds send-rights to permitted I/O only)  │  (motion control)      │
├─────────────────────────────────────────────┴────────────────────────┤
│  I/O Device Server  (GPIO / Fieldbus / CANopen / PROFIBUS / EtherCAT)│
│  Watchdog / Supervisor Server                                         │
│  Scan-Cycle Timer Server (deterministic period)                       │
├──────────────────────────────────────────────────────────────────────┤
│  MACH MICROKERNEL  (IPC · VM · Tasks · Threads · RT Scheduler)       │
├──────────────────────────────────────────────────────────────────────┤
│  Industrial Hardware (x86 IPC, ARM Cortex-A, RISC-V)                 │
└──────────────────────────────────────────────────────────────────────┘
```

**Key benefit over Linux-based PLCs (CODESYS, OpenPLC on Raspberry Pi):**  
A faulting PLC program task is killed and restarted by the supervisor server.  It cannot
corrupt the kernel, the I/O server, or other program tasks.  On Linux, a bug in a real-time
thread can deadlock the kernel or corrupt shared memory visible to other tasks.

**Risks / open problems:**
- Scan-cycle determinism requires hard-RT work (§3.5).
- Fieldbus protocol stacks (EtherCAT, PROFIBUS) would need porting as device servers.
- IEC 61131-3 compliance certification (e.g., IEC 61508 SIL) would require formal verification
  of the kernel TCB — a major research undertaking.

---

### 3.7 Virtual PLC Runtime

**Status:** Concept; closer than hardware PLC because timing constraints are relaxed.

A **virtual PLC** (vPLC) runs IEC 61131-3 programs in a software container, typically for:
- Simulation and testing of PLC programs before deployment
- Soft-PLC running on an industrial PC alongside SCADA
- Digital-twin environments
- CI/CD pipelines for PLC logic (run unit tests against virtual I/O)

For a vPLC, the I/O device server replaces hardware I/O with a simulated model:

```
┌─────────────────────────────────────────────────────────────────┐
│  Virtual PLC on NEOMACH                                           │
├────────────────────────────────────────────────────────────────-┤
│  PLC Program Task (IEC 61131-3 interpreter or compiled LD/ST)   │
├─────────────────────────────────────────────────────────────────┤
│  Virtual I/O Server  ← I/O state modelled in memory            │
│  Test Harness Server ← injects test vectors, reads outputs      │
├─────────────────────────────────────────────────────────────────┤
│  MACH MICROKERNEL                                               │
└─────────────────────────────────────────────────────────────────┘
```

Because vPLC timing need not be cycle-accurate to microseconds, the Phase 2 soft-RT scheduler
is sufficient.  The vPLC use case could be prototyped earlier than the hardware PLC target.

**Integration opportunity:** Run NEOMACH vPLC inside a QEMU VM on a standard Linux CI server to
execute PLC unit tests as part of a standard Git-triggered pipeline.

---

### 3.8 Hypervisor / Type-1 VMM

**Status:** Research concept.

A Mach kernel's IPC + VM primitives are also the building blocks of a thin Type-1 hypervisor.
Each virtual machine is a Mach task with:
- Its own address-space (mapped by the Mach VM subsystem)
- A device server acting as its virtual hardware (virtio, emulated UART, etc.)
- IPC for hypercalls (replacing `vmcall` / `hvc` instructions)

NEOMACH is *not* designed as a hypervisor today, but the architecture does not prevent it.
Historical precedent: the OSF MK kernel was used as a hypervisor substrate for the
Mach User-Level VMs ("UX" servers), and HURD's Hurd-on-Mach concept is a direct ancestor.

---

## 4. Processor Architecture Requirements

### 4.1 Currently Supported Architectures

| Architecture | Directory | Status | Notes |
|-------------|-----------|--------|-------|
| **x86-64** (AMD64 / Intel 64) | `kernel/platform/x86_64/` | Phase 1 primary | Full 4-level paging, APIC, SMP planned |
| **AArch64** (ARMv8-A, ARMv9-A) | `kernel/platform/aarch64/` | Phase 2 (boot + UART working) | 4-level translation tables, GIC planned |

### 4.2 Minimum Hardware Requirements

The following are the *absolute minimums* for the NEOMACH kernel to boot.  Server workloads
increase these requirements considerably.

| Resource | Absolute Minimum (kernel only) | Practical Minimum (kernel + servers) |
|----------|-------------------------------|--------------------------------------|
| **CPU** | 64-bit, hardware MMU, privilege levels (ring 0/3 or EL0/EL1) | Same; multiple cores desirable |
| **RAM** | 16 MB | 64 MB (headless); 256 MB (with desktop) |
| **Storage** | None (kernel can boot from memory) | 512 MB (minimal install) |
| **Timer** | One programmable periodic timer (APIC timer / ARM generic timer) | Same |
| **Console** | Serial UART (8250/16550 or PL011) | Same; framebuffer optional |

#### Why 64-bit?

The Phase 1 kernel targets 64-bit architectures because:
1. 64-bit virtual address spaces simplify the higher-half kernel layout.
2. Modern embedded SoCs (Cortex-A55+, RISC-V RV64) are 64-bit even in constrained devices.
3. 32-bit Mach ports (mach_port_name_t is `uint32_t`) are already present in the type headers;
   a 32-bit port for Cortex-M or ARMv7 is *architecturally possible* but requires a separate
   platform port and has not been designed yet.

#### Why no MMU-less support?

The Mach VM subsystem is fundamentally page-table driven.  Without an MMU, task isolation
(the core of Mach's security model) is impossible to enforce in hardware.  MMU-less
microcontrollers (Cortex-M0/M0+/M3 without MPU, most 8-bit and 16-bit MCUs) are out of scope
for a full NEOMACH port.

The closest analogue for MMU-less environments is an MPU-based Mach-like kernel (similar to
seL4 on Cortex-M with MPU), which would be a separate research project.

### 4.3 Porting to New Architectures

The platform-independent kernel code (`kernel/ipc/`, `kernel/vm/`, `kernel/kern/`) is written
in C99 with no architecture assumptions beyond:

- `uintptr_t` fits a virtual address
- `atomic_flag` for spinlocks (C11 atomics)
- A calling convention compatible with the NEOMACH ABI

To port NEOMACH to a new architecture, implement the following in
`kernel/platform/<new-arch>/`:

| File | Responsibility |
|------|---------------|
| `boot.S` | Reset vector, stack setup, call to `kernel_main()` |
| `platform.c` | UART console, memory map discovery, timer init |
| `pmap.c` | Page table create/destroy, map/unmap, TLB flush |
| `context_switch.S` | Save/restore CPU registers for thread switch |
| `trap.c` | Exception/interrupt dispatch to Mach trap handlers |

See `kernel/platform/aarch64/` for a working reference implementation.

---

## 5. Memory Model Limits

### Virtual Address Space

| Architecture | Kernel VA | User VA | Total VA |
|-------------|-----------|---------|----------|
| x86-64 (4-level paging) | `0xffff_8000_0000_0000` – `0xffff_ffff_ffff_ffff` (128 TB) | `0x0000_0000_0000_0000` – `0x0000_7fff_ffff_ffff` (128 TB) | 256 TB |
| x86-64 (5-level paging, future) | 64 PB kernel | 64 PB user | 128 PB |
| AArch64 (4-level, 48-bit) | `0xffff_0000_0000_0000` – `0xffff_ffff_ffff_ffff` (256 TB) | `0x0000_0000_0000_0000` – `0x0000_ffff_ffff_ffff` (256 TB) | 512 TB |

### Physical Memory

- **Phase 1:** Physical memory allocator uses a simple bitmap.  Practical limit is whatever
  the firmware's memory map reports — typically up to 64 GB without special handling.
- **Phase 2+:** A zone allocator (following CMU Mach's `zalloc`) will replace the bitmap;
  NUMA awareness is future work.
- **Absolute limit:** `vm_offset_t` / `vm_size_t` are `uintptr_t` — 64-bit on 64-bit
  platforms, giving an 18.4 EB (exabyte) theoretical maximum.

### IPC Message Size

- **Phase 1 limit:** Inline message body ≤ 1024 bytes (`IPC_MQUEUE_MAX_MSG_SIZE`).
- **Phase 2:** Out-of-line memory descriptors will remove this limit; large payloads will
  be transferred via VM page remapping (copy-on-write), matching Mach 3.0 behavior.

### Port Namespace

- **Phase 1:** 256 port names per task (flat array).
- **Phase 2:** Dynamic hash table; practical limit becomes available kernel memory.

---

## 6. IPC Throughput Limits

IPC performance is a critical metric for microkernel viability.  The Mach IPC fast path is
the most performance-sensitive code in the kernel.

### Theoretical bounds (Phase 1, copy semantics)

| Metric | Estimate | Note |
|--------|----------|------|
| Small message round-trip (< 64 B) | ~1–5 µs | Two syscalls + two copies + scheduler overhead |
| Large message (1024 B) | ~10–50 µs | Dominated by two 1 KB copies |
| Context switch overhead | ~1–2 µs | Platform-dependent |

*These are estimates.  The NEOMACH design principle "Measure Before Compromising" requires
benchmarking on real hardware.  See `tests/ipc/ipc_perf.c`.*

### Comparison targets

| System | Small-message IPC round-trip |
|--------|------------------------------|
| L4Ka::Pistachio | ~100–200 ns |
| seL4 (verified) | ~300–500 ns |
| QNX Neutrino | ~1–2 µs |
| Mach 3.0 (original) | ~20–50 µs |
| NEOMACH Phase 1 (estimate) | ~1–5 µs |
| NEOMACH Phase 2 target | < 1 µs (zero-copy fast path) |

Mach 3.0's IPC was notoriously slower than L4 due to copy semantics.  NEOMACH Phase 2 will add
the L4-inspired zero-copy fast path for short messages, closing the gap significantly.

---

## 7. Scheduling Limits

### Phase 1

- Single-core, cooperative round-robin.
- No preemption.
- No priority.
- Suitable only for kernel bring-up / smoke tests.

### Phase 2 Target

- Preemptive, priority-based (fixed-priority, similar to POSIX `SCHED_FIFO`).
- Multi-core SMP support.
- Priority inheritance for IPC-held locks.

### Phase 3+ (RT) Target

- Rate-monotonic analysis (RMA) support.
- Earliest-Deadline First (EDF) scheduler.
- Priority ceiling protocol.
- Latency budget: < 50 µs interrupt-to-thread for "soft RT"; < 10 µs for "hard RT" (requires
  additional platform-level work such as disabling SMI, using PREEMPT_RT-equivalent patches).

---

## 8. What the Kernel Deliberately Does NOT Do

Understanding these exclusions is critical to correctly evaluating NEOMACH for a deployment.

| Capability | Why it is absent | Where it lives instead |
|-----------|-----------------|------------------------|
| POSIX syscalls (`read`, `write`, `fork`, …) | Not a kernel responsibility | BSD server (`servers/bsd/`) |
| Filesystems | Not a kernel responsibility | VFS server (`servers/vfs/`) |
| TCP/IP networking | Not a kernel responsibility | Network server (`servers/network/`) |
| Device drivers | Not a kernel responsibility | Device server (`servers/device/`) |
| Signals (SIGKILL, SIGTERM, …) | POSIX abstraction, not Mach | BSD server |
| Sockets | POSIX abstraction | Network server |
| Named pipes / FIFOs | POSIX abstraction | VFS server |
| `mmap` of files | VM + VFS interaction | BSD + VFS servers cooperate via external pager protocol |
| Dynamic linking | Userspace concern | Not yet implemented |

**Implication for embedded / RTOS / PLC deployments:** You do *not* need a BSD server.  You
do not need POSIX.  You can deploy NEOMACH with only the servers your application requires,
making the software stack dramatically smaller and more auditable than a Linux-based solution.

---

## 9. Decision Guide: Is NEOMACH Right for My Project?

```
Does your target CPU have an MMU?
    NO  →  NEOMACH is not suitable.  Consider Zephyr, FreeRTOS, or seL4 on Cortex-M with MPU.
   YES  →  Continue.

Is your CPU 64-bit (x86-64 or AArch64)?
    NO  →  A platform port is needed (see §4.3).  Significant work; contact maintainers.
   YES  →  Continue.

Do you need hard real-time guarantees (< 10 µs latency)?
   YES  →  NEOMACH Phase 2+ with RT scheduler work is required.
            As an interim, consider AMP pairing with a Cortex-M/R bare-metal RT core.
    NO  →  Continue.

Do you need IEC 61131-3 PLC compliance or SIL certification today?
   YES  →  NEOMACH is not yet suitable.  It is a research/prototype platform.
    NO  →  Continue.

Do you want fault-isolated, capability-secured userspace services on a small footprint?
   YES  →  NEOMACH is well-suited.  Choose your server set from:
            - bootstrap only (kernel tests / bringup)
            - bootstrap + device (embedded sensor node)
            - bootstrap + device + network (IoT gateway)
            - bootstrap + device + network + BSD (POSIX app server)
            - full stack (desktop / workstation)
    NO  →  A simpler RTOS (Zephyr, FreeRTOS) may be sufficient.
```

---

## 10. Platform Examples

### 10.1 x86-64 Desktop / Workstation

**Hardware:** Any x86-64 PC with UEFI or GRUB2, ≥ 256 MB RAM.

**Boot path:** GRUB2/UEFI → Multiboot2 → `kernel/platform/x86_64/boot.S` → `kernel_main()`.

**Typical server set:** full stack (BSD + VFS + device + network + display + GNUstep).

**Development reference:** QEMU `-machine q35 -cpu qemu64` is the primary test target.

```sh
# Build and run under QEMU (x86-64)
cmake -S kernel -B build \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/x86_64-elf-clang.cmake \
  -DNEOMACH_BOOT_TESTS=ON
cmake --build build
tools/run-qemu.sh
```

---

### 10.2 ARM Cortex-A (Raspberry Pi, BeagleBone, NXP i.MX)

**Hardware examples:**

| Board | SoC | CPU | RAM |
|-------|-----|-----|-----|
| Raspberry Pi 4 Model B | BCM2711 | 4× Cortex-A72 (ARMv8-A) | 1–8 GB |
| Raspberry Pi 5 | BCM2712 | 4× Cortex-A76 (ARMv8.2-A) | 4–8 GB |
| BeagleBone AI-64 | TDA4VM | 2× Cortex-A72 | 4 GB |
| NXP i.MX 8M Plus | i.MX 8M Plus | 4× Cortex-A53 | 2–6 GB |
| NVIDIA Jetson Orin NX | Orin | 8× Cortex-A78AE | 8–16 GB |
| Khadas VIM4 | Amlogic A311D2 | 4× Cortex-A73 + 4× Cortex-A53 | 8 GB |

**Boot path:** U-Boot → EFI stub or FIT image → `kernel/platform/aarch64/boot.S`.

**Status:** `aarch64/boot.S` and PL011 UART console are implemented.  MMU and full pmap
are Phase 2 work.

**Build (AArch64 cross-compile):**

```sh
# Using an aarch64-elf-clang toolchain (adjust path as needed)
cmake -S kernel -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/aarch64-elf-clang.cmake
cmake --build build-aarch64

# Test under QEMU (no physical board required)
qemu-system-aarch64 \
  -machine virt -cpu cortex-a57 -nographic \
  -kernel build-aarch64/neomach.elf
```

**Embedded server set:** bootstrap + device (GPIO/I2C/SPI via `/dev/mem` until device server
is written) + optional network (lwIP).

---

### 10.3 ARM Cortex-M (Microcontrollers — Exploratory)

**Hardware examples:** STM32F7/H7 (Cortex-M7), RP2350 (Cortex-M33).

**Caveats:**
- Cortex-M does **not** have a full MMU — only an optional MPU (Memory Protection Unit).
- Without an MMU, true Mach task isolation is hardware-enforced only at MPU granularity
  (8 or 16 regions, no per-page protection).
- A full NEOMACH port is **not** planned for Cortex-M.
- A *research* lightweight variant using MPU regions instead of page tables would be a
  separate project (see [seL4 on Cortex-M](https://sel4.systems/Info/Hardware/cortexm.pml)
  for prior art).

**Recommendation:** Use Zephyr RTOS (which supports Cortex-M with optional MPU isolation)
for microcontroller targets.  NEOMACH can act as the gateway/controller kernel on an adjacent
Cortex-A core in a heterogeneous SoC.

---

### 10.4 RISC-V (SiFive, StarFive, Microchip PolarFire)

**Hardware examples:**

| Board | SoC | CPU | RAM |
|-------|-----|-----|-----|
| StarFive VisionFive 2 | JH7110 | 4× U74 (RV64GC) | 4–8 GB |
| SiFive HiFive Unmatched | FU740 | 4× U74 + 1× S7 | 16 GB |
| Microchip Icicle Kit | PolarFire SoC | 4× U54 + 1× E51 | 2 GB |
| Milk-V Pioneer | SG2042 | 64× C910 (RV64) | 128 GB |

**RISC-V MMU:** Sv39 (39-bit VA, 3-level page tables), Sv48, or Sv57 modes.  All are
compatible with the Mach VM model.

**Port status:** No RISC-V platform code exists yet.  The port would require:
1. `kernel/platform/riscv64/boot.S` — M-mode / S-mode entry, SBI calls
2. `platform.c` — SBI console, timer (via `mtime`/`mtimecmp`)
3. `pmap.c` — Sv39/Sv48 page table management
4. `context_switch.S` — RV64 register save/restore

RISC-V is a high-priority future target given the ecosystem's rapid growth in both edge
computing (JH7110) and server-class (SG2042) silicon.

---

### 10.5 FPGA Soft-Core CPU (Xilinx MicroBlaze, Lattice RISC-V)

**Use case:** Custom hardware prototyping, FPGA-accelerated edge compute.

**Viable path:** A RISC-V soft-core (e.g., VexRiscv, BOOM, CVA6) synthesized on an FPGA
(Xilinx Zynq, Lattice ECP5) can run the RISC-V NEOMACH port once it exists.  This enables
tightly coupled hardware accelerators (in FPGA fabric) to be exposed as device-server
endpoints over Mach IPC.

**Not immediately viable:** MicroBlaze and other non-RISC-V soft-cores would need a
dedicated platform port.

---

### 10.6 Industrial PC (IPC) / Beckhoff CX / Siemens IPC

**Hardware examples:**

| Product | CPU | Typical Use |
|---------|-----|-------------|
| Beckhoff CX2040 | Intel Core i7 (x86-64) | EtherCAT master + SCADA |
| Siemens IPC427E | Intel Core i5/i7 (x86-64) | Soft-PLC, SCADA, HMI |
| Kontron KBox A-203 | ARM Cortex-A9 | Industrial gateway |
| Phoenix Contact ILC 2050 | ARM Cortex-A9 | PROFINET PLC |

**NEOMACH fit:** Industrial PCs running x86-64 or AArch64 can boot NEOMACH from a standard
UEFI bootloader.  The PLC runtime server set (§3.6) would replace the BSD/desktop stack.

**Advantage over Linux-PREEMPT_RT (CODESYS, OpenPLC):** Kernel TCB is orders of magnitude
smaller; a PLC program bug cannot panic the kernel.

**Missing today:** EtherCAT / PROFIBUS / PROFINET device server drivers.

---

### 10.7 Automotive-Grade SoC (NXP S32G, Renesas R-Car)

**Hardware examples:**

| SoC | CPU | Use Case |
|-----|-----|---------|
| NXP S32G274A | 4× Cortex-A53 + 3× Cortex-M7 | Vehicle service-oriented gateway |
| Renesas R-Car H3 | 4× Cortex-A57 + 4× Cortex-A53 | ADAS domain controller |
| TI TDA4VM | 2× Cortex-A72 + 6× Cortex-R5F | ADAS + functional safety |
| NXP i.MX 95 | 6× Cortex-A55 + Cortex-M33 | Zonal ECU |

**NEOMACH fit:**
- The A-class cores run NEOMACH (AArch64 port).
- The M/R-class cores run a hard-RT RTOS (FreeRTOS, SafeRTOS) for safety-critical loops.
- Mach IPC over shared memory (inter-processor communication) bridges the two domains.
- AUTOSAR Adaptive Platform personalities could be implemented as NEOMACH servers.

**Caveats:** Automotive-grade certification (ISO 26262 ASIL-B/D) requires formal
verification of the kernel TCB — far beyond current project scope.  NEOMACH would serve as
a research/prototype platform in this domain.

---

## 11. Comparison with Other Approaches

| System | Kernel Type | RT Support | MMU Required | IPC Mechanism | Typical Use |
|--------|------------|-----------|--------------|---------------|-------------|
| **NEOMACH** | Mach microkernel | Planned (Phase 2+) | Yes | Mach ports | Research OS, desktop, edge, embedded |
| Linux (PREEMPT_RT) | Monolithic | Soft/hard RT patch | Yes | syscalls, sockets, pipes | Server, embedded, industrial |
| QNX Neutrino | Microkernel | Hard RT | Yes | Mach-like messages | Automotive, medical, industrial |
| seL4 | Microkernel (verified) | Hard RT possible | Yes (or MPU) | Synchronous IPC endpoints | Safety-critical, automotive, defense |
| Zephyr RTOS | Monolithic RTOS | Hard RT | No (MPU optional) | Queues, pipes, semaphores | Microcontroller, IoT |
| FreeRTOS | RTOS | Hard RT | No | Queues, semaphores | Microcontroller |
| HURD | Mach microkernel | None | Yes | Mach ports | Research, GNU desktop |
| GNU Mach | Mach microkernel | None | Yes | Mach ports | HURD substrate |

**NEOMACH's differentiator vs. GNU Mach / HURD:** Modern C99 codebase, clean-room implementation
of the Mach ABI, NeXT/OpenStep framework integration, designed from the start with embedded
and edge targets in mind.

**NEOMACH's differentiator vs. QNX:** Open-source (GPL-2.0+), full-stack NeXT heritage, not
commercially licensed.

**NEOMACH's differentiator vs. seL4:** Not formally verified (a research target, not a
certified product); far more developer-accessible; full POSIX personality available.

---

## 12. Roadmap for Non-Desktop Targets

The items below are required before NEOMACH can be seriously deployed in non-desktop domains.
They are listed in rough priority order.

| Item | Domain Enabled | Estimated Phase |
|------|---------------|-----------------|
| AArch64 MMU + full pmap | Embedded, edge, Raspberry Pi | Phase 2 |
| Preemptive SMP scheduler | All multi-core targets | Phase 2 |
| Priority inheritance IPC | RT, PLC, automotive | Phase 2+ |
| RISC-V platform port | Edge (JH7110, FU740), FPGA | Phase 3 |
| Blocking IPC + port sets | All production targets | Phase 2 |
| `vm_wire` / memory locking | RT, PLC | Phase 2+ |
| RT scheduling policy server (RMA/EDF) | Hard RT, PLC | Phase 3 |
| EtherCAT / CANopen device server | Industrial PLC | Phase 4+ |
| IEC 61131-3 runtime server | PLC | Phase 4+ |
| Formal TCB specification | Safety-critical (SIL, ASIL) | Research / external |

---

## 13. References

- Accetta et al., *"Mach: A New Kernel Foundation for UNIX Development"*, USENIX 1986.
- Tevanian et al., *"Mach Threads and the Unix Kernel: The Battle for Control"*, USENIX 1987.
- Liedtke, *"On µ-kernel Construction"*, SOSP 1995 — motivation for L4-class IPC performance.
- Klein et al., *"seL4: Formal Verification of an OS Kernel"*, SOSP 2009.
- Heiser & Elphinstone, *"L4 Microkernels: The Lessons from 20 Years of Research and Deployment"*, TOCS 2016.
- OSF/RI MK series documentation — OSF Mach 3.0 implementation notes (`archive/osf-mk/`).
- CMU Mach 3.0 design: `archive/cmu-mach/`.
- IEC 61131-3:2013 — *Programmable controllers — Part 3: Programming languages*.
- IEC 61508:2010 — *Functional safety of E/E/PE safety-related systems*.
- QNX Neutrino RTOS Architecture Guide — https://www.qnx.com/developers/docs/
- Zephyr Project Documentation — https://docs.zephyrproject.org/
- seL4 Reference Manual — https://sel4.systems/Info/Docs/seL4-manual-latest.pdf
- ARM Architecture Reference Manual (ARMv8-A) — https://developer.arm.com/
- RISC-V Privileged Architecture Specification — https://github.com/riscv/riscv-isa-manual
