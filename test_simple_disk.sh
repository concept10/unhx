#!/bin/bash
# Simple disk I/O test script

QEMU="qemu-system-x86_64"
KERNEL="/Users/tracey/Developer/unhx/build/kernel/unhx.elf"
INITRD="/Users/tracey/Developer/unhx/build/user/init.elf"
DISK="/tmp/unhx-test-disk.img"

# Create disk image
dd if=/dev/zero of="$DISK" bs=1M count=10 2>/dev/null

# Boot QEMU with disk
$QEMU -machine q35 -accel tcg -cpu qemu64 -m 256M \
    -kernel "$KERNEL" -initrd "$INITRD" \
    -drive file="$DISK",if=virtio,format=raw \
    -serial stdio -display none \
    -no-reboot -no-shutdown \
    2>&1 | head -200
