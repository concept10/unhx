# servers/vfs/

Virtual filesystem server — translator model (HURD-inspired).

## Responsibility

Provide a unified virtual filesystem namespace. Any process with appropriate
port rights can mount a filesystem translator, following HURD's elegant model.

## Implementation Plan

- [ ] ramfs — in-memory filesystem (first target, needed for Phase 2 shell)
- [ ] VFS namespace — mount points, path resolution
- [ ] Translator protocol — port-based mount interface
- [ ] `open()`, `read()`, `write()`, `close()`, `stat()`, `readdir()`
- [ ] File descriptor ↔ port mapping (bridge to BSD server)

## Phase 3+

- [ ] AHCI/NVMe block device integration (via device server)
- [ ] ext2/ext4 filesystem translator
- [ ] ISO 9660 (CD-ROM)
