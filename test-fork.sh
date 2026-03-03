#!/bin/bash
cd /Users/tracey/Developer/unhx

# Run QEMU in background, capturing output
./tools/run-qemu.sh --no-build > /tmp/qemu_output.txt 2>&1 &
QEMU_PID=$!

# Wait for the boot sequence to complete (enough time to see fork/wait test)
sleep 35

# Kill QEMU
kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

# Show the relevant output
echo "=== QEMU Output (userspace section) ==="
tail -200 /tmp/qemu_output.txt | grep -A 50 "userspace task running" || tail -100 /tmp/qemu_output.txt
