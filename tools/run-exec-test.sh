#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
killall qemu-system-x86_64 >/dev/null 2>&1 || true
expect <<'EOF'
set timeout 220
spawn ./tools/run-qemu.sh --no-build
expect "unhox$ "
send "/bin/init.elf\r"
expect {
    "Hello from userspace!" { puts "EXEC_OK"; exit 0 }
    timeout { puts "EXEC_TIMEOUT"; exit 1 }
}
EOF
