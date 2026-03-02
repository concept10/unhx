# IPC Performance Baseline

Benchmark results for UNHOX Mach IPC, measured during Phase 1 kernel development.

## Environment

| Parameter | Value |
|-----------|-------|
| Host | macOS arm64 (Apple Silicon) |
| Emulator | QEMU 9.x, TCG software emulation |
| CPU model | `qemu64` (generic x86-64) |
| RAM | 256 MB |
| Measurement | x86-64 TSC (`rdtsc`) |
| Message size | 32 bytes (header + 4-byte seq field) |
| Iterations | 50 per benchmark |

## Results (QEMU TCG, Phase 1)

| Operation | Min (cycles) | Avg (cycles) | Max (cycles) |
|-----------|-------------|-------------|-------------|
| Send | 5,000 | 6,520 | 13,000 |
| Receive | < 1,000 | 600 | 8,000 |
| Round-trip | 6,000 | 6,780 | 21,000 |

### Interpretation

- **Send** (~6,500 cycles avg): Dominated by `ipc_space_lookup()` (O(1) array
  index), capability check, `kalloc()` for the `ipc_kmsg` (~1040 bytes), and
  `kmemcpy()` to copy the message into the kernel buffer.

- **Receive** (~600 cycles avg): Much cheaper than send because it only
  dequeues from a linked list and copies out. No allocation is needed. The
  `kfree()` call is a no-op in Phase 1, saving deallocation overhead.

- **Round-trip** (~6,780 cycles avg): Close to send cost since receive adds
  little overhead. This is the single-threaded, same-context measurement.
  Real two-task round-trip will add two context switches (~1,000-2,000 cycles
  each on bare metal).

### Caveats

These results are **lower-bound estimates** due to Phase 1 simplifications:

1. **No blocking**: Send/receive return immediately. Phase 2 blocking IPC will
   add thread sleep/wakeup overhead.
2. **No contention**: Single-threaded execution. No spinlock contention on
   port or queue locks.
3. **kfree is a no-op**: Deallocation overhead is zero. A real zone allocator
   will add cost to receive.
4. **QEMU TCG**: Software CPU emulation. TSC values are approximate and do not
   map directly to real hardware cycle counts.
5. **No context switch**: Both tasks run in the same kernel context. A real
   two-task round-trip requires two context switches.

## Comparison to Historical Mach Systems

| System | Round-trip (cycles) | Notes |
|--------|-------------------|-------|
| CMU Mach 3.0 (i386) | ~100,000 | Full blocking IPC, 1990s hardware |
| L4 (Pentium) | ~800 | Optimised register-based IPC |
| XNU (modern x86-64) | ~2,000-5,000 | Mach + BSD fast traps |
| UNHOX Phase 1 (QEMU) | ~6,780 | Non-blocking, no context switch |

The UNHOX number is not directly comparable since it measures only the
kernel-internal message copy path without context switches or blocking.
Phase 2 numbers with blocking IPC and real context switches will be the
meaningful baseline.

## Future Benchmarks

- **Phase 2**: Re-run with blocking IPC, preemptive scheduling, and real
  two-task context switches.
- **Bare metal**: Run on physical x86-64 hardware for accurate cycle counts.
- **Message size sweep**: Measure cost vs. message size (32B to 1024B).
- **Queue depth**: Measure send cost as queue depth increases.
- **Contention**: Measure with multiple senders to a single port.

## Reproducing

```sh
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
      -DUNHOX_BOOT_TESTS=ON
cmake --build build
./tools/run-qemu.sh --no-build
```

The performance results appear at the end of the serial output.
