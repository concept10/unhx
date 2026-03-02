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

## Phase 1 Test Plan

- [ ] Boot kernel under QEMU and verify serial output
- [ ] Create two tasks and verify they run
- [ ] Pass a Mach message between two tasks and verify receipt
- [ ] Verify IPC port right lifecycle (create, transfer, destroy)
- [ ] Verify VM map create/destroy

## IPC Performance Benchmark (Phase 1+)

The proposal specifically calls out measuring IPC performance on modern hardware
before making any architectural compromises. The benchmark will:

1. Measure round-trip latency for a null Mach message (synchronous send+receive)
2. Compare against HURD/GNU Mach baseline
3. Compare against Linux `pipe()` round-trip (not a fair comparison, but useful reference)
4. Profile where cycles are spent
