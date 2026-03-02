# servers/device/

Device server — hardware device abstraction and driver management.

## Responsibility

Isolate all hardware drivers in userspace. The kernel exposes raw interrupt
delivery and DMA buffer management; the device server wraps these into
higher-level device port interfaces.

## Phase 3 Target Drivers

- [ ] PCI bus enumeration
- [ ] AHCI SATA disk controller
- [ ] NVMe block device
- [ ] virtio-blk and virtio-net (QEMU acceleration)
- [ ] USB HID (keyboard + mouse)
- [ ] VESA/GOP framebuffer

## References

- XNU `iokit/` — driver framework reference
- GNU Mach device interface headers
