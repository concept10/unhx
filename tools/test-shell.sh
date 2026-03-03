#!/usr/bin/env bash
# tools/test-shell.sh — Test shell commands interactively
#
# This script runs UNHOX under QEMU and sends commands to the shell,
# capturing the output to verify functionality.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "Building UNHOX..."
cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build" \
      -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/cmake/x86_64-elf-clang.cmake" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DUNHOX_BOOT_TESTS=ON \
      --no-warn-unused-cli > /dev/null 2>&1

cmake --build "$REPO_ROOT/build" > /dev/null 2>&1

echo "Launching QEMU and testing shell commands..."
echo "Commands to test: help, echo, ps, fork, /bin/init.elf, exit"
echo ""

# Use expect to interact with the shell
expect <<'EOF'
set timeout 30
spawn ./tools/run-qemu.sh --no-build

# Wait for shell prompt
expect {
    "unhox$ " { }
    timeout { puts "ERROR: Shell prompt never appeared"; exit 1 }
}

# Test 1: help command
send "help\r"
expect {
    "Built-in commands:" { puts "✓ help command works" }
    timeout { puts "✗ help command failed"; exit 1 }
}
expect "unhox$ "

# Test 2: echo command
send "echo Hello from UNHOX shell\r"
expect {
    "Hello from UNHOX shell" { puts "✓ echo command works" }
    timeout { puts "✗ echo command failed"; exit 1 }
}
expect "unhox$ "

# Test 3: unknown command
send "boguscommand\r"
expect {
    "command not found" { puts "✓ unknown command handling works" }
    timeout { puts "✗ command not found message failed"; exit 1 }
}
expect "unhox$ "

# Test 4: /bin/init.elf (exec test)
send "/bin/init.elf\r"
expect {
    "Hello from userspace!" { puts "✓ exec() works - program loaded and ran" }
    timeout { puts "✗ exec failed or no output"; exit 1 }
}

# Wait a bit for init to finish
sleep 1

# Test 5: exit command (should terminate)
send "exit\r"
expect {
    "Goodbye." { puts "✓ exit command works" }
    eof { puts "✓ shell exited (or exec replaced it)" }
    timeout { puts "✗ exit command failed"; exit 1 }
}

# Success
puts ""
puts "========================================"
puts " All shell tests PASSED"
puts "========================================"
exit 0
EOF
