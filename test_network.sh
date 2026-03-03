#!/bin/bash
# Test virtio-net network driver

QEMU="qemu-system-x86_64"
KERNEL="/Users/tracey/Developer/unhx/build/kernel/unhx.elf"
INITRD="/Users/tracey/Developer/unhx/build/user/init.elf"

# Boot QEMU with virtio-net device
$QEMU -machine q35 -accel tcg -cpu qemu64 -m 256M \
    -kernel "$KERNEL" -initrd "$INITRD" \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
    -serial stdio -display none \
    -no-reboot -no-shutdown \
    2>&1 | head -250
