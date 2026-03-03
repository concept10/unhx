#!/usr/bin/env bash
# Simple exec test - run for 15 seconds and capture output
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "Launching QEMU for 15 seconds..."
gtimeout 15s expect <<'EOF' 2>&1 | tee /tmp/exec_test_output.txt || true
set timeout 15
spawn ./tools/run-qemu.sh --no-build

# Wait for shell prompt
expect {
    "unhox$ " { send "/bin/init.elf\r" }
    timeout { puts "ERROR: Timeout waiting for prompt"; exit 1 }
}

# Wait for output
sleep 3

# Exit
send "exit\r"
expect eof
EOF

echo ""
echo "=== Exec-related output ==="
grep -E "\[bsd-exec\]|exec" /tmp/exec_test_output.txt || echo"(no exec messages found)"
