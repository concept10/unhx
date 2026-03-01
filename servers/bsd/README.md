# servers/bsd/

BSD personality server — POSIX syscall emulation over Mach IPC.

## Responsibility

Provide a POSIX-compatible process model to userspace programs without putting
any BSD code inside the kernel. This is the hardest server to implement correctly.

## POSIX Subsystems to Implement

- [ ] Process lifecycle — `fork()`, `exec()`, `exit()`, `wait()`
- [ ] Signal delivery across task boundaries
- [ ] File descriptor table (backed by VFS server)
- [ ] `dup()`, `pipe()`, `select()`/`poll()`
- [ ] Basic `/proc`-style introspection
- [ ] `getpid()`, `getuid()`, credentials model

## Hard Problems (informed by HURD experience)

- **Signal delivery**: signals must interrupt blocking Mach IPC calls
- **Fork semantics**: cloning address space across server boundary
- **Blocking syscalls**: emulating `read()`/`write()` without in-kernel scheduler integration

## References

- GNU HURD `hurd/` — reference for multi-server POSIX emulation
- Utah Lites — BSD server on Mach 3.0 reference (`archive/utah-oskit/`)
- Mach 3.0 Server Writer's Guide
