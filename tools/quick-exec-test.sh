#!/usr/bin/env bash
# Quick exec test with timeout
cd "$(dirname "$0")/.."

# Run QEMU with a hard 12-second timeout
( sleep 12; killall qemu-system-x86_64 2>/dev/null ) &
KILLER_PID=$!

./tools/run-qemu.sh --no-build 2>&1 | tee /tmp/exec-debug.txt &
QEMU_PID=$!

# Wait 8 seconds for boot
sleep 8

# Send the exec command
echo "/bin/init.elf" > /dev/null

# Wait a bit more for output
sleep 3

# Kill everything
kill $KILLER_PID 2>/dev/null
killall qemu-system-x86_64 2>/dev/null

echo ""
echo "=== Debug output related to exec ==="
grep -E "\[bsd-exec\]|\[bsd-vfs" /tmp/exec-debug.txt | tail -30
