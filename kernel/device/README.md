# kernel/device/ — Device Drivers for UNHOX

## Overview

Phase 3 device layer supporting PCI device enumeration and Virtio device communication for QEMU q35 machine.

## Components

### PCI (`pci.h`, `pci.c`)

Minimal PCI configuration space access and device enumeration:
- x86 I/O port-based configuration access (CF8/CFC)
- Bus 0 enumeration (32 slots × 8 functions)
- BAR (Base Address Register) extraction
- Interrupt routing setup

**Limitations (Phase 3):**
- Single bus (bus 0 only)
- No hotplug
- No bridge traversal

### Virtio Infrastructure (`virtio.h`)

Structures for Virtio 1.0 devices:
- Virtual queue descriptors, available ring, used ring
- Feature negotiation framework
- Configuration space layout (device-independent)

**Supported device types:**
- VIRTIO_TYPE_BLOCK — block devices
- VIRTIO_TYPE_NET — network devices (device-independent structure only)

### Virtio Block Driver (`virtio_blk.h`, `virtio_blk.c`)

Block device driver for QEMU virtio-blk:
- Device detection (PCI vendor 0x1AF4, device 0x1001)
- Feature negotiation
- Queue initialization
- Capacity discovery

**Phase 3 implementation:**
- Device detection and initialization only
- Read/write stubs (return success without I/O)
- Full queue management deferred to Phase 3b

## Build Integration

Device sources are compiled into kernel:
```cmake
device/pci.c
device/virtio_blk.c
```

Initialization in `kern/kern.c`:
```c
pci_init();               /* Enumerate PCI devices */
virtio_blk_init();        /* Detect and init virtio-blk */
```

## Testing

Runtime boot log shows:
```
[pci] enumerating devices
[pci] device 1af4:1001 at 0:3.0
[pci] found 1 devices
[virtio-blk] initializing
[virtio-blk] device at BAR0=...
[virtio-blk] host features: ...
[virtio-blk] queue 0 size: 256
[virtio-blk] capacity: 2097152 sectors
[virtio-blk] device ready
```

## Next Steps (Phase 3b)

1. **Full queue management** — implement descriptor/avail/used ring operations
2. **Synchronous I/O** — implement `virtio_blk_read_sectors()` / `virtio_blk_write_sectors()`
3. **Interrupt handling** — wire device interrupts to handle completed requests
4. **DMA buffer management** — handle physically contiguous buffer requirements
5. **VFS integration** — plug virtio-blk into BSD/VFS server as backing store

## References

- PCI Local Bus Specification 3.0
- Virtio Specification 1.0 (https://docs.oasis-open.org/virtio/virtio/v1.0/)
- QEMU q35 chipset documentation
