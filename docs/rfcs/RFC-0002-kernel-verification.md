# RFC-0002: Kernel Functional Correctness — Verification and Testing Strategy

- **Status**: Draft
- **Author**: UNHOX Project
- **Date**: 2026-03-07

## Summary

This RFC describes a layered strategy for establishing functional correctness and
security guarantees for the UNHOX Mach microkernel. The approach is inspired by
the seL4 project's end-to-end formal proof and the L4 family's tradition of
specifying kernel behaviour with mathematical rigour. We describe the properties
to be proved or tested, the tools to be used at each layer, and a phased
implementation plan tied to the existing UNHOX milestone schedule.

## Motivation

A microkernel's value proposition is a small, verifiable trusted-computing base.
If the kernel is not demonstrably correct, the security boundary it provides is
illusory. The seL4 team showed that a production-quality microkernel can be
fully formally verified; UNHOX should follow the same intellectual tradition even
if a complete mechanised proof is deferred to later phases.

The properties we care about fall into two categories:

1. **Functional correctness** — the implementation does what the specification
   says. If `mach_msg_send` returns `KERN_SUCCESS`, the message was enqueued on
   the correct port.

2. **Security properties** — the kernel enforces isolation and capability
   confinement. A task cannot read another task's memory, cannot forge a port
   name, and cannot receive messages not addressed to a port it holds a receive
   right for.

## Background: seL4 and the L4 Verification Tradition

The seL4 microkernel (Klein et al., 2009) provides the most complete example of
mechanised kernel verification to date. The proof proceeds in three layers:

```
Abstract Spec  ──refinement──>  Executable Spec  ──refinement──>  C Code
(Isabelle/HOL)                  (Haskell/Isabelle)                 (gcc/clang)
```

Key results published by the seL4 team:

- **Functional correctness**: The C implementation is a refinement of the
  Isabelle abstract specification. Every observable behaviour of the C code is
  permitted by the spec.
- **Integrity**: Untrusted code cannot modify kernel or other tasks' data except
  through authorised IPC.
- **Confidentiality (information flow)**: High-security data cannot flow to
  low-security tasks. Proved using the `Noninterference` policy in Isabelle.
- **Binary-level proof**: The machine-code binary produced by gcc/clang is
  proved correct down to the instruction set, closing the compiler gap.

UNHOX does not aim to replicate the full seL4 proof overnight. Instead, this RFC
defines a graduated programme that starts with well-engineered automated tests
and model checks, then adds mechanised proofs for the most security-critical
paths as the kernel matures.

## Properties to Establish

### 1. IPC Capability Safety

**Informal property**: A task can send to a port if and only if it holds a send
right to that port in its `ipc_space`.

**Formal statement** (first-order logic sketch):

```
∀ task T, port P, message M:
  mach_msg_send(T, P, M) = KERN_SUCCESS
  ⟹ has_send_right(T, P)
```

And conversely (no spurious failures):

```
∀ task T, port P, message M:
  has_send_right(T, P) ∧ queue_not_full(P)
  ⟹ mach_msg_send(T, P, M) = KERN_SUCCESS
```

### 2. Message Integrity

**Informal property**: The message received is byte-for-byte identical to the
message sent. No field is silently dropped or corrupted.

```
∀ task T_send, T_recv, port P, message M:
  has_send_right(T_send, P) ∧ has_receive_right(T_recv, P)
  ∧ mach_msg_send(T_send, P, M) = KERN_SUCCESS
  ∧ mach_msg_receive(T_recv, P, &M') = KERN_SUCCESS
  ⟹ M = M'
```

### 3. Receive Right Exclusivity

**Informal property**: Exactly one task holds the receive right to a port at any
point in time. Two tasks cannot simultaneously dequeue from the same port.

```
∀ port P, task T1, T2:
  has_receive_right(T1, P) ∧ has_receive_right(T2, P)
  ⟹ T1 = T2
```

### 4. Memory Isolation

**Informal property**: A task cannot read or write virtual addresses that belong
to another task's `vm_map`.

```
∀ task T1, T2, address A:
  T1 ≠ T2 ∧ mapped_in(T2, A)
  ⟹ ¬ accessible(T1, A)
```

### 5. Name Unforgeability

**Informal property**: Port names are kernel-assigned integers that only resolve
inside the task they were assigned to. A task cannot construct or guess a valid
port name in another task's space.

### 6. Kernel Non-Interference

**Informal property** (seL4-style): The execution of task T1 cannot affect the
observable output of task T2 unless T1 holds a send right to a port that T2
receives on. This is the information-flow security property.

## Verification and Testing Layers

We adopt a four-layer strategy, applied incrementally across UNHOX milestones.

```
Layer 4: Mechanised Proof (Isabelle/HOL or Lean 4)   ← Phase 3+
Layer 3: Model Checking (TLA+ / SPIN)                ← Phase 2
Layer 2: Property-Based Testing (QuickCheck/Hypothesis)  ← Phase 1 (now)
Layer 1: Unit & Integration Tests                    ← Phase 1 (now)
```

### Layer 1 — Unit and Integration Tests

**Status**: Partially implemented (`kernel/tests/ipc_test.c`).

Unit tests exercise individual subsystem functions with known inputs and check
expected outputs. Integration tests boot the kernel under QEMU and verify
end-to-end behaviour via serial output.

**Existing coverage**:

| Test | File | Property |
|------|------|----------|
| IPC smoke test | `kernel/kern/kernel_task.c` | Basic send/receive |
| IPC milestone test | `kernel/tests/ipc_test.c` | Magic-value round-trip |

**Planned additions** (Phase 1 completion):

```
tests/unit/ipc/
├── test_ipc_space.c         -- alloc/lookup/free of port names
├── test_ipc_port.c          -- port lifecycle: create, send, receive, destroy
├── test_ipc_right.c         -- right copy, move, destroy; refcount invariants
├── test_ipc_mqueue.c        -- enqueue/dequeue; full-queue failure
└── test_mach_msg.c          -- send+receive round-trip; header field preservation

tests/unit/vm/
├── test_vm_page.c           -- physical page alloc/free; double-free detection
└── test_vm_map.c            -- map create/destroy; overlap detection

tests/integration/
├── boot/boot_test.sh        -- QEMU boot; verify serial banner
├── ipc/ipc_roundtrip.sh     -- QEMU IPC test; verify PASS output
└── vm/vm_isolation.sh       -- QEMU VM test; verify isolation result
```

**Test harness convention** (host-side unit tests):

```c
/*
 * tests/unit/ipc/test_ipc_space.c
 * Compile with: cc -I kernel/include -DUNIT_TEST test_ipc_space.c
 * Kernel dependencies are stubbed in tests/unit/stubs/.
 */
#include "harness.h"
#include "ipc/ipc_space.h"

TEST(alloc_and_lookup_port_name) {
    struct ipc_space *sp = ipc_space_create();
    mach_port_name_t name;

    kern_return_t r = ipc_entry_alloc(sp, &name);
    ASSERT_EQ(r, KERN_SUCCESS);
    ASSERT_NE(name, MACH_PORT_NULL);

    struct ipc_entry *e = ipc_entry_lookup(sp, name);
    ASSERT_NE(e, NULL);

    ipc_space_destroy(sp);
}
```

### Layer 2 — Property-Based Testing

Property-based tests generate thousands of randomised inputs and check that
invariants hold for all of them. This catches corner cases that hand-written
unit tests miss.

**Tool**: We use a C port of QuickCheck style, or the kernel subsystems are
compiled as host-mode libraries and tested with
[Hypothesis](https://hypothesis.readthedocs.io/) (Python) via a thin FFI shim.

**Properties to check automatically**:

```python
# tests/property/test_ipc_properties.py
from hypothesis import given, strategies as st
from unhox_ipc_shim import ipc_space, ipc_port, mach_msg_send, mach_msg_receive

@given(st.binary(max_size=1000))
def test_message_integrity(payload):
    """Any payload sent is received unchanged."""
    space_a, space_b, port, send_name, recv_name = make_pair()
    send_msg(space_b, send_name, payload)
    received = recv_msg(space_a, recv_name)
    assert received == payload

@given(st.integers(min_value=0, max_value=255),
       st.integers(min_value=0, max_value=255))
def test_send_requires_right(forged_name, task_id):
    """Sending to a name not in the sender's space always fails."""
    space = ipc_space()
    result = mach_msg_send(space, forged_name, b"hello")
    assert result != KERN_SUCCESS

@given(st.lists(st.binary(max_size=900), max_size=16))
def test_queue_ordering(messages):
    """Messages are dequeued in the order they were enqueued (FIFO)."""
    space_a, space_b, port, send_name, recv_name = make_pair()
    for m in messages:
        send_msg(space_b, send_name, m)
    received = [recv_msg(space_a, recv_name) for _ in messages]
    assert received == messages
```

**Key invariants to encode as properties**:

1. `alloc` followed by `free` leaves `ipc_space` in original state (no leak).
2. Sending to a dead port returns `MACH_SEND_INVALID_DEST`.
3. `mach_port_allocate` always produces a name not currently in use.
4. The receive-right count on any port is always ≤ 1.
5. Destroying a port while a message is in flight does not crash the kernel;
   the in-flight message is silently dropped or returns a dead-name error.

### Layer 3 — Model Checking

Model checking exhaustively explores all reachable states of an abstract model
of the kernel to verify safety and liveness properties.

#### TLA+ Model of IPC

We write a TLA+ specification of the port capability model. TLA+ is well suited
to Mach IPC because the rights table is a simple function and transitions are
well-defined.

```tla
--------------------------- MODULE MachIPC ---------------------------
EXTENDS Naturals, FiniteSets

CONSTANTS Tasks, Ports, NullPort

VARIABLES
    send_rights,   \* send_rights[t][p] = TRUE if task t holds send right to p
    recv_right,    \* recv_right[p] = task that holds receive right to p
    queue          \* queue[p] = sequence of pending messages

TypeInvariant ==
    /\ send_rights \in [Tasks -> [Ports -> BOOLEAN]]
    /\ recv_right  \in [Ports -> Tasks \cup {NullPort}]
    /\ \A p \in Ports : Len(queue[p]) <= MAX_QUEUE_DEPTH

\* Safety: only one receiver per port
ExclusiveReceive ==
    \A p \in Ports :
        Cardinality({t \in Tasks : recv_right[p] = t}) <= 1

\* Safety: send requires right
SendRequiresRight ==
    \A t \in Tasks, p \in Ports :
        Send(t, p, m) => send_rights[t][p]

\* Liveness: if a right exists and queue not full, send eventually succeeds
SendEventuallySucceeds ==
    \A t \in Tasks, p \in Ports, m \in Messages :
        (send_rights[t][p] /\ Len(queue[p]) < MAX_QUEUE_DEPTH)
        ~> KERN_SUCCESS \in possible_results(Send(t, p, m))
=======================================================================
```

The TLC model checker (included with the TLA+ toolbox) will exhaustively verify
`ExclusiveReceive` and `SendRequiresRight` for small finite instances, then the
proofs can be lifted to the full model using TLAPS (TLA+ Proof System).

#### SPIN/Promela Model of Lock-Free IPC Path

For the spinlock-based synchronisation in `ipc_mqueue.c`, we write a Promela
model and verify with SPIN that:

- No deadlock exists under concurrent send/receive.
- The queue invariant (size ≤ MAX_QUEUE_DEPTH) is preserved.
- No message is dropped when the queue has space.

```promela
/* Simplified Promela model of ipc_mqueue enqueue/dequeue */
#define MAX_DEPTH 16
int queue[MAX_DEPTH];
int head = 0, tail = 0, count = 0;
bit lock = 0;

inline acquire() { atomic { lock == 0 -> lock = 1 } }
inline release() { lock = 0 }

proctype Sender(int msg) {
    acquire();
    assert(count <= MAX_DEPTH);  /* queue invariant */
    if
    :: count < MAX_DEPTH ->
        queue[tail] = msg;
        tail = (tail + 1) % MAX_DEPTH;
        count++;
    :: else -> skip  /* MACH_SEND_NO_BUFFER */
    fi;
    release();
}

proctype Receiver() {
    acquire();
    if
    :: count > 0 ->
        int m = queue[head];
        head = (head + 1) % MAX_DEPTH;
        count--;
    :: else -> skip  /* MACH_RCV_TOO_LARGE */
    fi;
    release();
}

init {
    run Sender(42);
    run Sender(99);
    run Receiver();
    run Receiver();
}
```

Running `spin -run -safety model.pml` checks all interleavings.

### Layer 4 — Mechanised Proof (Isabelle/HOL or Lean 4)

This layer provides machine-checked proofs with the highest assurance level.
It is planned for Phase 3+ when the kernel API stabilises.

#### Approach: Refinement in Isabelle/HOL

Following the seL4 methodology:

1. **Abstract specification** — the kernel is specified as a state-transition
   system in Isabelle/HOL. States are mathematical objects (sets, functions);
   transitions are total functions from state × input to state × output.

   ```isabelle
   (* Abstract state *)
   record ipc_state =
     rights :: "task ⇒ port ⇒ right_set"
     queues :: "port ⇒ message list"
     recv   :: "port ⇒ task option"

   (* Send transition *)
   fun abstract_send :: "ipc_state ⇒ task ⇒ port ⇒ message
                         ⇒ ipc_state × kern_return_t" where
     "abstract_send s t p m =
       (if SEND ∈ (rights s t p) ∧ length (queues s p) < MAX_DEPTH
        then (s ⦇ queues := (queues s)(p := queues s p @ [m]) ⦈, KERN_SUCCESS)
        else (s, MACH_SEND_NO_BUFFER))"

   (* Correctness property: right required *)
   theorem send_requires_right:
     "fst (abstract_send s t p m) ≠ s ∨ snd (abstract_send s t p m) = KERN_SUCCESS
      ⟹ SEND ∈ rights s t p"
   ```

2. **Executable specification** — a functional model of the C code, written in
   Isabelle's code-generation subset. AutoCorres (or Lean4's `clang2lean`)
   extracts this from the C source automatically.

3. **Refinement proof** — a bisimulation proof that every execution of the
   executable spec is permitted by the abstract spec.

4. **Security proofs** — using Isabelle's `Noninterference` framework, prove
   that the kernel satisfies the Murray et al. intransitive noninterference
   policy (the basis of the seL4 confidentiality proof).

#### Alternative: Lean 4

[Lean 4](https://lean4.github.io/) is a modern interactive theorem prover that
is gaining traction for systems verification. The `clang2lean` toolchain
(in development) can translate C to Lean 4 verification conditions. UNHOX may
target Lean 4 for its tighter integration with modern proof automation (Aesop,
Omega, Decide tactics).

#### Scope for Phase 3

The first mechanised proof targets the IPC subsystem only (≈ 800 lines of C):

| Module | Lines | Proof Target |
|--------|-------|--------------|
| `ipc/ipc.c` | ~200 | Space/port allocation invariants |
| `ipc/ipc_mqueue.c` | ~120 | Queue FIFO + size invariant |
| `ipc/ipc_kmsg.c` | ~180 | Send/receive capability check |
| `ipc/ipc_right.c` | ~150 | Right exclusivity; refcount ≥ 0 |
| Total | ~650 | IPC capability safety |

The VM subsystem (memory isolation) follows in Phase 4.

## Fuzz Testing

Fuzz testing complements formal methods by finding implementation bugs that
proofs might miss if the model is incomplete.

**Tool**: `libFuzzer` (built into Clang) or AFL++ targeting kernel subsystem
code compiled in host mode with sanitisers.

```sh
# Compile IPC subsystem with AddressSanitizer + libFuzzer
clang -fsanitize=address,fuzzer -DUNIT_TEST \
  -I kernel/include kernel/ipc/*.c tests/fuzz/fuzz_ipc.c \
  -o fuzz_ipc

# Run fuzzer
./fuzz_ipc -max_total_time=3600 corpus/ipc/
```

A fuzzing harness (`tests/fuzz/fuzz_ipc.c`) feeds random byte streams as
message payloads and verifies that the kernel never:

- Dereferences a null pointer
- Reads or writes out of bounds (caught by ASan)
- Corrupts the `ipc_space` (verified by a post-call consistency check)

## CI Integration

Each layer maps to a CI job in `.github/workflows/`:

| Job | Trigger | Tools |
|-----|---------|-------|
| `unit-tests` | Every push | `make -C tests/unit` on x86-64 host |
| `qemu-boot` | Every push | QEMU + `tests/integration/boot/boot_test.sh` |
| `property-tests` | Every push | Python + Hypothesis shim |
| `model-check` | Nightly | TLA+ TLC, SPIN |
| `fuzz-ipc` | Weekly | libFuzzer, 1 hour budget |
| `isabelle-proof` | On spec change | Isabelle/HOL batch session |

```yaml
# .github/workflows/unit-tests.yml (excerpt)
jobs:
  unit:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install clang
        run: sudo apt-get install -y clang lld
      - name: Build kernel (with boot tests)
        run: |
          cmake -S kernel -B build \
            -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/x86_64-elf-clang.cmake \
            -DUNHOX_BOOT_TESTS=ON
          cmake --build build
      - name: Host unit tests
        run: make -C tests/unit
```

## Phased Implementation Plan

| Phase | Layer | Deliverable |
|-------|-------|-------------|
| 1 (now) | L1 | Expand `kernel/tests/ipc_test.c`; add `tests/unit/ipc/` |
| 1 (now) | L1 | QEMU boot and IPC round-trip CI job |
| 2 | L2 | Python/Hypothesis property tests for IPC and VM |
| 2 | L3 | TLA+ IPC model; TLC verification for small instances |
| 2 | L3 | SPIN model for `ipc_mqueue` concurrency |
| 3 | L4 | Isabelle/HOL abstract spec for IPC |
| 3 | L4 | Refinement proof: abstract spec ↔ C (IPC module) |
| 3 | L1/L2 | Fuzz testing with libFuzzer + ASan |
| 4 | L4 | VM isolation proof |
| 5 | L4 | Noninterference (information-flow) proof |

## Open Questions

1. **Proof assistant choice**: Isabelle/HOL has the most mature systems-proof
   tooling (seL4, CakeML). Lean 4 has better ergonomics and growing ecosystem.
   Decision deferred to Phase 3.

2. **C dialect**: The IPC source must remain in a well-defined C subset for
   AutoCorres/c2lean to parse it. We currently use C11; confirm that
   `_Atomic` usage does not complicate extraction.

3. **Compiler correctness**: seL4 closes the compiler gap with a binary proof.
   UNHOX will initially trust the compiler (Clang) and use UBSan/ASan to
   validate code-gen assumptions.

4. **Scheduler** (Phase 2): Round-robin scheduling introduces concurrency that
   the Phase 1 model elides. The Phase 2 TLA+/SPIN models must handle
   preemption.

## References

- Klein et al., "seL4: Formal Verification of an OS Kernel" (SOSP 2009)
- Murray et al., "seL4: From General Purpose to a Proof of Information Flow
  Enforcement" (IEEE S&P 2013)
- Liedtke, "On µ-Kernel Construction" (SOSP 1995)
- Accetta et al., "Mach: A New Kernel Foundation for UNIX Development" (1986)
- Lamport, "Specifying Systems: The TLA+ Language and Tools" (2002)
- Ben-Ari, "Principles of the Spin Model Checker" (2008)
- AutoCorres: `https://trustworthy.systems/projects/autocorres/`
- Lean 4 theorem prover: `https://lean4.github.io/`
- seL4 proof repository: `https://github.com/seL4/l4v`
