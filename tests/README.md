# tests/

Kernel test harness and QEMU-based integration tests.

## Test Categories

| Directory | Contents |
|-----------|----------|
| `unit/` | Unit tests for kernel subsystems (run on host via mocking) |
| `integration/` | Full system tests running under QEMU |
| `ipc/` | IPC correctness and performance tests |
| `vm/` | Virtual memory tests |
| `scripts/` | QEMU automation scripts |

## Running Tests

```sh
# Unit tests (no QEMU required)
make -C tests/unit

# Integration tests (requires QEMU)
./tests/scripts/run-all.sh
```

## Phase 1 Test Status ✅

- [x] Boot kernel under QEMU and verify serial output *(2026-03-01)*
- [x] Create two tasks and verify they run
- [x] Pass a Mach message between two tasks and verify receipt *(13/13 tests PASS)*
- [ ] Automated `tests/integration/boot/boot_test.sh` — QEMU boot smoke test (CI)
- [ ] Verify IPC port right lifecycle (create, transfer, destroy) — unit test
- [ ] Verify VM map create/destroy — unit test

## IPC Tests (`tests/ipc/`)

| File | Status | Description |
|------|--------|-------------|
| `ipc_roundtrip_test.c` | ✅ Done | Two-task message-passing correctness test — 5 scenarios |
| `ipc_perf.c` | ✅ Done | Null Mach message round-trip benchmark (TSC-based) |

## IPC Performance Benchmark

The proposal specifically calls out measuring IPC performance on modern hardware
before making any architectural compromises. The benchmark:

1. Measures round-trip latency for a null Mach message (synchronous send+receive)
2. Uses the x86-64 TSC (`rdtsc`) for cycle-accurate timing
3. Reports min/max/average over multiple iterations

Planned comparisons (Phase 2+):

- Compare against HURD/GNU Mach baseline
- Compare against Linux `pipe()` round-trip (not a fair comparison, but useful reference)
- Profile where cycles are spent

Results will be documented in `docs/research/ipc-performance.md`.
