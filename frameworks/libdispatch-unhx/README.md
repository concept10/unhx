# libdispatch-unhx — Grand Central Dispatch for UNHOX

UNHOX-specific libdispatch integration layer. These files are **not** part of the upstream Apple swift-corelibs-libdispatch submodule—they implement GCD semantics directly on top of UNHOX's Mach IPC primitives.

## Architecture

**Queues:** Each `dispatch_queue_t` owns a Mach port. A dedicated worker thread loops on `mach_msg_recv_user()`, receiving work items as Mach messages.

**Groups:** `dispatch_group_t` uses an atomic counter + notify port. `enter()` increments, `leave()` decrements and signals on zero.

**Blocks:** `blocks_runtime.c` provides `_Block_copy()` and `_Block_release()` to support Clang's Blocks extension (closures in C).

## Files

- `blocks_runtime.c/h` — Blocks runtime (copy/release, class symbols)
- `dispatch.h` — Public GCD API
- `dispatch_queue.c` — Mach port-backed queues (`dispatch_async_f`, `dispatch_sync_f`)
- `dispatch_group.c` — Work group coordination (`dispatch_group_enter/leave/wait`)

## Design Decisions

1. **Mach-native:** Uses `mach_msg_*_user()` syscalls directly instead of emulating POSIX threads/mutexes
2. **Thread-per-queue:** Simple initial model; each queue spawns a dedicated worker thread
3. **Function-based API:** Uses `_f` variants (function pointer + context) for simplicity

## Integration

These files are compiled into the UNHOX userspace libraries. See `user/CMakeLists.txt` for build integration.

## Future Work

- Block-based API (`dispatch_async()` with `^{ }` syntax)
- Thread pool optimization (shared workers across queues)
- Priority-based scheduling
- Integration with Foundation's `NSOperationQueue`
