#!/usr/bin/env bash
# tests/integration/boot/boot_test.sh — QEMU boot smoke test for UNHOX
#
# Builds the kernel, boots it under QEMU, captures serial output, and
# verifies that all Phase 1 milestones pass.
#
# Usage:
#   ./tests/integration/boot/boot_test.sh [--no-build]
#
# Exit code:
#   0 — all checks passed
#   1 — one or more checks failed
#
# This is the local equivalent of .github/workflows/boot-test.yml.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/../../.. && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
KERNEL_IMG="${BUILD_DIR}/kernel/unhx.elf"
SERIAL_OUT="$(mktemp)"
QEMU_TIMEOUT=15
DO_BUILD=1

for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

cleanup() {
    rm -f "${SERIAL_OUT}"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if [[ $DO_BUILD -eq 1 ]]; then
    echo "[boot_test] Building kernel..."

    # Detect toolchain file
    TOOLCHAIN="${REPO_ROOT}/cmake/x86_64-elf-clang.cmake"
    CMAKE_EXTRA=()
    if [[ -f "${TOOLCHAIN}" ]]; then
        CMAKE_EXTRA+=(-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}")
    fi

    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        "${CMAKE_EXTRA[@]}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUNHOX_BOOT_TESTS=ON \
        --no-warn-unused-cli >/dev/null 2>&1

    cmake --build "${BUILD_DIR}" --target unhx.elf >/dev/null 2>&1
fi

if [[ ! -f "${KERNEL_IMG}" ]]; then
    echo "[boot_test] FAIL: kernel image not found at ${KERNEL_IMG}"
    exit 1
fi

# ---------------------------------------------------------------------------
# Boot under QEMU
# ---------------------------------------------------------------------------
echo "[boot_test] Booting kernel under QEMU (${QEMU_TIMEOUT}s timeout)..."

# Detect acceleration
ACCEL_FLAGS=(-accel tcg -cpu qemu64)
if [[ -w /dev/kvm ]] 2>/dev/null; then
    ACCEL_FLAGS=(-accel kvm -cpu host)
fi

qemu-system-x86_64 \
    -machine q35 \
    "${ACCEL_FLAGS[@]}" \
    -m 256M \
    -kernel "${KERNEL_IMG}" \
    -serial file:"${SERIAL_OUT}" \
    -display none \
    -no-reboot \
    -no-shutdown &
QEMU_PID=$!

# Wait for QEMU with timeout
ELAPSED=0
while kill -0 "${QEMU_PID}" 2>/dev/null && [[ $ELAPSED -lt $QEMU_TIMEOUT ]]; do
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

# Kill QEMU if still running
if kill -0 "${QEMU_PID}" 2>/dev/null; then
    kill "${QEMU_PID}" 2>/dev/null
    wait "${QEMU_PID}" 2>/dev/null || true
else
    wait "${QEMU_PID}" 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# Verify serial output
# ---------------------------------------------------------------------------
echo "[boot_test] Serial output captured ($(wc -l < "${SERIAL_OUT}") lines)"
echo ""

PASS=0
FAIL=0

check() {
    local label="$1"
    local pattern="$2"

    if grep -q "${pattern}" "${SERIAL_OUT}"; then
        echo "  [PASS] ${label}"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] ${label}"
        FAIL=$((FAIL + 1))
    fi
}

check "Kernel boots"                    "kernel_main entered"
check "IPC smoke test passes"           "Phase 1 complete. Mach IPC operational"
check "Bootstrap server initialises"    "bootstrap server ready"
check "IPC milestone v0.2 passes"       "IPC milestone v0.2 PASSED"
check "All milestone tests pass"        "All milestone tests PASSED"

echo ""
echo "=========================================="
echo " Results: ${PASS} passed, ${FAIL} failed"

if [[ $FAIL -eq 0 ]]; then
    echo " STATUS: PASS"
    echo "=========================================="
    exit 0
else
    echo " STATUS: FAIL"
    echo "=========================================="
    echo ""
    echo "Serial output:"
    cat "${SERIAL_OUT}"
    exit 1
fi
