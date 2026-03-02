# tests/

Kernel test assets for UNHOX, primarily QEMU-based integration coverage.

## Test Categories

| Path | Contents |
|------|----------|
| `tests/integration/boot/` | Boot smoke test script (`boot_test.sh`) |
| `kernel/tests/` | In-kernel milestone tests (`ipc_test.c`, `ipc_perf.c`) compiled when `UNHOX_BOOT_TESTS=ON` |

## Running Tests

```sh
# Configure and build with boot tests enabled
cmake -S . -B build_status \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
  -DUNHOX_BOOT_TESTS=ON
cmake --build build_status --target unhx.elf init.elf

# Integration boot smoke test
./tests/integration/boot/boot_test.sh
```

## Phase 1 Test Plan

- [x] Boot kernel under QEMU and verify serial output
- [x] Create two tasks and verify they run
- [x] Pass a Mach message between two tasks and verify receipt
- [x] Verify IPC port right lifecycle (create, transfer, destroy)
- [ ] Verify VM map create/destroy

## IPC Performance Benchmark (Phase 1+)

The proposal specifically calls out measuring IPC performance on modern hardware
before making any architectural compromises. The benchmark will:

1. Measure round-trip latency for a null Mach message (synchronous send+receive)
2. Compare against HURD/GNU Mach baseline
3. Compare against Linux `pipe()` round-trip (not a fair comparison, but useful reference)
4. Profile where cycles are spent
