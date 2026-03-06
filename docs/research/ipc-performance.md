# IPC Performance Analysis

## UNHOX Mach IPC — Phase 1 Performance Baseline

**Document type:** Research / Benchmarking  
**Phase:** 1 (Kernel Core)  
**Status:** Baseline established; Phase 2 targets set  
**Last updated:** 2026-03

---

## 1. Overview

IPC overhead is the historically dominant criticism of Mach-derived microkernel
systems.  The original CMU Mach 3.0 implementation showed round-trip IPC
latencies of 50–200 µs on the hardware of its era (VAX 8600, Sun-3), which was
significantly worse than BSD inter-process pipe communication on the same systems.
This "IPC tax" was the primary justification for Apple collapsing BSD back into
the XNU kernel and for the GNU HURD project's slow progress.

UNHOX takes the position articulated in the project proposal §7.3: *"We will
not repeat XNU's mistake of collapsing the architecture for performance reasons
before understanding the actual performance characteristics on modern hardware."*

This document records the methodology, measurements, and analysis of UNHOX IPC
performance, beginning with the Phase 1 kernel-internal baseline.

---

## 2. Background: The IPC Performance Problem

### 2.1 CMU Mach 3.0 (1986–1991)

The original IPC overhead came from several sources:

| Source | Description |
|--------|-------------|
| Memory copy | Messages copied twice: user → kernel, kernel → user |
| Complex type system | Typed descriptors required expensive parsing |
| Separate send + receive | Two syscalls per RPC = two kernel crossings |
| Thread scheduling | Naive FIFO scheduling caused unnecessary context switches |
| No fast path | Every message went through the same slow generic path |

Typical round-trip on Sun SPARC (1991): **100–500 µs**

### 2.2 L4 Microkernel (Liedtke, 1993)

Jochen Liedtke's L4 demonstrated that microkernel IPC *could* be fast by
redesigning the IPC path from scratch around the following principles:

1. **Minimise the kernel path**: The IPC implementation must fit in the L1
   instruction cache. If the IPC code doesn't fit in cache, it's too big.

2. **Registers for small messages**: Parameters that fit in CPU registers
   should never be written to memory. L4 passes small messages entirely
   in registers, eliminating the copy overhead.

3. **Combined send+receive**: One syscall performs both the send and the
   blocking wait for a reply. This halves the syscall overhead for the
   common synchronous RPC pattern.

4. **Synchronous rendezvous**: The kernel directly transfers a message from
   sender to receiver in one step if the receiver is already waiting
   (the "direct transfer" or "lazy copying" optimisation).

5. **Handoff scheduling**: When a sender blocks, the kernel immediately
   schedules the receiver rather than going through the general scheduler.

Result: L4 achieved round-trip times of **~1–5 µs** on the same hardware
where Mach achieved 100 µs — a 20–100x improvement.

**Reference**: Liedtke, "Improving IPC by Kernel Design" (SOSP 1993).

### 2.3 seL4 / Modern Microkernels (2009–present)

seL4 built on L4's work with formal verification and achieved round-trip times
of **~0.5–1 µs** on modern ARM Cortex-A and x86-64 hardware.

**Reference**: Heiser & Leslie, "The seL4 Microkernel: An Introduction" (2010).

### 2.4 GNU Mach / HURD (2000–present)

GNU Mach, as used by HURD, retained the original Mach IPC design without
adopting L4's optimisations. Measured round-trip times on modern hardware:

- **~10–30 µs** on x86-64 under KVM (reported by HURD developers, 2015–2020)
- Still 10–30x slower than L4, but far better than 1991 Mach on modern CPUs

This demonstrates that **modern hardware eliminates much of the original Mach
performance disadvantage** even without algorithmic improvements.

---

## 3. UNHOX Phase 1 Benchmark Methodology

### 3.1 Benchmark: Null Mach Message Round-Trip

The standard IPC micro-benchmark is the **null round-trip**:

```
┌─────────────┐         ┌─────────────┐
│   task_b    │         │   task_a    │
│  (sender)   │         │  (receiver) │
└──────┬──────┘         └──────┬──────┘
       │                       │
   t=0 │  mach_msg_send(port)  │
       │──────────────────────>│
       │                       │  mach_msg_receive(port)
       │                       │  [no reply in null test]
   t=T │                       │
```

For the round-trip (RPC) test:

```
┌─────────────┐         ┌─────────────┐
│   client    │         │   server    │
└──────┬──────┘         └──────┬──────┘
       │                       │
   t=0 │  SEND(request_port)   │
       │──────────────────────>│
       │                       │  RECEIVE(request_port)
       │                       │  SEND(reply_port)
       │  RECEIVE(reply_port)  │
   t=T │<──────────────────────│
```

Metric: **T** (nanoseconds) = `cycles_per_roundtrip / cpu_freq_ghz`

### 3.2 Platform

All measurements are taken with the following setup:

| Parameter | Value |
|-----------|-------|
| Architecture | x86-64 |
| Execution environment | QEMU 8.x (KVM-accelerated) |
| CPU model | Reported by QEMU (`-cpu host`) |
| Memory | 256 MiB (per `qemu-run.sh`) |
| Timer | x86 TSC (`rdtsc` instruction) |
| TSC mode | Constant (assumed; modern x86 CPUs) |

**Note**: Phase 1 does not calibrate the TSC. Raw cycle counts are reported.
To convert: `nanoseconds = cycles / (CPU_GHz)`. On a 3 GHz processor,
1000 cycles ≈ 333 ns.

### 3.3 Benchmark Code

The benchmark is implemented in `tests/ipc/ipc_perf.c` and called from
`kernel_main()` when `UNHOX_BOOT_TESTS=ON`. It:

1. Creates two kernel tasks (no context switch overhead in Phase 1)
2. Allocates a port pair using `ipc_right_alloc_receive` + `ipc_right_copy_send`
3. Sends N null messages (header only, 24 bytes) from task_b to task_a
4. Receives each message at task_a
5. Records TSC timestamps around the loop
6. Reports average cycles per round-trip

Phase 1 does **not** measure:
- Syscall entry/exit overhead (no userspace yet)
- Context switch costs (single-threaded Phase 1)
- Cache miss effects (cold-cache scenario)

These will be measured in Phase 2 with real userspace tasks.

---

## 4. Phase 1 Baseline Results

*Results to be filled in after first QEMU boot.*

| N (iterations) | Total cycles | Cycles/round-trip | Notes |
|----------------|-------------|-------------------|-------|
| 10             | TBD         | TBD               | Cold cache |
| 100            | TBD         | TBD               | Warming |
| 1000           | TBD         | TBD               | Steady state |

### Expected Range

Based on the Phase 1 implementation:

- **Send path**: `ipc_space_lock` → lookup entry → check right → `ipc_mqueue_send`
  (lock mqueue → allocate kmsg → memcpy 24 bytes → enqueue → unlock)
- **Receive path**: `ipc_space_lock` → lookup entry → check right →
  `ipc_mqueue_receive` (lock mqueue → dequeue kmsg → memcpy 24 bytes →
  free kmsg → unlock)

Estimated: **200–2000 cycles** per direction, so **400–4000 cycles** per
round-trip. On a 3 GHz processor: **130 ns – 1.3 µs**.

This is Phase 1's theoretical floor (no syscalls, no context switches,
in-kernel execution). The real-system overhead in Phase 2 will be higher.

---

## 5. IPC Layers in UNHOX

The UNHOX IPC subsystem is structured in layers, from highest to lowest:

```
┌─────────────────────────────────────────────────────────┐
│  Layer 4: Userspace / Server Interface                  │
│  mach_msg() system call → kernel trap                   │
│  [Phase 2: syscall entry via SYSCALL/SYSRET]            │
├─────────────────────────────────────────────────────────┤
│  Layer 3: mach_msg_trap() — Combined Send+Receive       │
│  kernel/ipc/mach_msg.c                                  │
│  Implements the L4-inspired single-syscall RPC          │
├─────────────────────────────────────────────────────────┤
│  Layer 2: Right Management — ipc_right.c                │
│  kernel/ipc/ipc_right.c                                 │
│  Allocate, copy, transfer, deallocate port rights       │
├─────────────────────────────────────────────────────────┤
│  Layer 1: Send/Receive — ipc_kmsg.c                     │
│  kernel/ipc/ipc_kmsg.c                                  │
│  mach_msg_send() / mach_msg_receive()                   │
│  Capability check + queue enqueue/dequeue               │
├─────────────────────────────────────────────────────────┤
│  Layer 0: Queue / Port — ipc_mqueue.c, ipc.c            │
│  kernel/ipc/ipc_mqueue.c, kernel/ipc/ipc.c             │
│  Raw port and message queue operations                  │
└─────────────────────────────────────────────────────────┘
```

This layering follows the principle from L4's design: each layer adds a
well-defined abstraction. Performance-critical paths can bypass upper layers
when correctness is guaranteed by the caller.

---

## 6. Phase 2 Optimisation Targets

Based on the L4 performance literature, the following optimisations are planned
for Phase 2 (in priority order):

### 6.1 Blocking Receive with Thread Wakeup (Critical Path)

**Current**: Phase 1 receive is non-blocking; returns error if queue empty.  
**Target**: Thread blocks on the port's mqueue; scheduler wakes it on message arrival.  
**Impact**: Enables correct synchronous IPC; eliminates busy-polling.  
**Mechanism**: `ipc_mqueue` gains a wait queue; blocked threads are linked there.

### 6.2 Combined Send+Receive (L4 "IPC Call")

**Current**: `mach_msg_trap` in Phase 1 does send then non-blocking receive.  
**Target**: Full blocking combined operation in one syscall.  
**Impact**: 50% reduction in syscall overhead for RPC workloads.

### 6.3 Handoff Scheduling

**Current**: Phase 1 has no preemptive scheduler.  
**Target**: When task A sends to task B (which is waiting), the kernel
  directly switches to task B without going through the general scheduler.  
**Impact**: Eliminates a context switch from every synchronous IPC.  
**Reference**: Liedtke (1993), §3.3 — Direct Process Switch.

### 6.4 Eliminate Double Copy for Small Messages

**Current**: Phase 1 copies user message into `ipc_kmsg.ikm_data[]` (kernel buffer),
  then copies from `ikm_data` to receiver buffer. Two copies.  
**Target**: For messages ≤ 128 bytes (fits in register-sized window):
  copy directly from sender stack to receiver buffer in one pass.  
**Impact**: 50% reduction in copy overhead for the common case.

### 6.5 VM-Based Zero-Copy for Large Messages (Out-of-Line Memory)

**Current**: Phase 1 limits messages to 1024 bytes, always copied.  
**Target**: Messages > 1 page use VM COW page remapping (Mach's OOL descriptor mechanism).  
**Impact**: O(1) cost for large transfers regardless of size.  
**Reference**: CMU Mach 3.0 paper §3.2 — Out-of-Line Memory.

---

## 7. Comparison Table (Historical + Projected)

| System | Platform | Round-Trip Latency | Notes |
|--------|----------|-------------------|-------|
| CMU Mach 3.0 | Sun SPARC (1991) | ~100–500 µs | Original implementation |
| GNU Mach | x86-64 / KVM (2020) | ~10–30 µs | Modern hardware saves 10x |
| L4 | x86 Pentium (1993) | ~1–5 µs | Register-based fast path |
| seL4 | ARM Cortex-A9 (2010) | ~0.5–1 µs | Formally verified L4 derivative |
| Linux pipe | x86-64 (2023) | ~4–8 µs | Reference (different semantics) |
| **UNHOX Phase 1** | x86-64 / QEMU | **TBD** | In-kernel, no syscall |
| **UNHOX Phase 2 target** | x86-64 / QEMU | **< 10 µs** | With blocking + handoff |
| **UNHOX Phase 2 target** | x86-64 bare metal | **< 3 µs** | With L4-style optimisations |

---

## 8. References

1. Accetta, M. et al. "Mach: A New Kernel Foundation for UNIX Development."
   USENIX Summer Conference, 1986.

2. Liedtke, J. "Improving IPC by Kernel Design."
   ACM SOSP, 1993. (The original L4 IPC performance paper.)

3. Liedtke, J. "On µ-Kernel Construction."
   ACM SOSP, 1995. (Defines the µ-kernel construction principles.)

4. Härtig, H. et al. "The Performance of µ-Kernel-Based Systems."
   ACM SOSP, 1997. (L4/Linux vs. native Linux benchmark.)

5. Heiser, G. and Leslie, B. "The seL4 Microkernel: An Introduction."
   NICTA Technical Report, 2010.

6. Shapiro, J. et al. "EROS: A Fast Capability System."
   ACM SOSP, 1999.

7. GNU Mach source: https://git.savannah.gnu.org/git/hurd/gnumach.git

8. seL4 source: https://github.com/seL4/seL4

9. UNHOX benchmark source: `tests/ipc/ipc_perf.c`
