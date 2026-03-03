#!/bin/bash
# Test script for UNHOX framebuffer driver
# Boots QEMU with video mode and captures both serial and graphical output

qemu-system-x86_64 \
    -machine q35 \
    -accel tcg \
    -cpu qemu64 \
    -m 256M \
    -kernel build/kernel/unhx.elf \
    -initrd build/user/init.elf \
    -vga std \
    -serial stdio \
    -no-reboot \
    -no-shutdown \
    | head -n 250
