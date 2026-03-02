#!/usr/bin/env bash
# tools/run-qemu.sh — Build UNHOX and launch it under QEMU
#
# Usage:
#   ./tools/run-qemu.sh [--no-build] [--no-kvm]
#
#   --no-build  Skip the CMake build step (use an existing build/unhx.elf)
#   --no-kvm    Force TCG software emulation even if KVM is available
#
# Prerequisites (macOS arm64):
#   brew install llvm qemu
#
# QEMU flags explained:
#   -machine q35        Modern Intel Q35 chipset emulation (PCIe, AHCI)
#   -cpu qemu64         Generic x86-64 CPU emulation (safe on all hosts)
#   -m 256M             256 MB RAM — enough for Phase 1 kernel development
#   -kernel <img>       Load a Multiboot2 kernel directly (no boot disk needed)
#   -serial stdio       Redirect the guest's COM1 (I/O port 0x3F8) to stdout
#   -display none       No graphical window — we rely on serial output only
#   -no-reboot          Exit QEMU instead of rebooting on triple fault
#   -no-shutdown        Do not power off on ACPI shutdown (keeps terminal open)
#   -s                  Enable the GDB stub on TCP port 1234 (shorthand for
#                       -gdb tcp::1234)
#   -accel kvm          Use KVM hardware acceleration (Linux only)
#   -accel tcg          Use TCG software emulation (default on macOS)
#   -accel hvf          Use macOS Hypervisor.framework (macOS only, x86-64 guests)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
KERNEL_IMG="${BUILD_DIR}/kernel/unhx.elf"
INITRD_IMG="${BUILD_DIR}/user/init.elf"

DO_BUILD=1
FORCE_TCG=0

for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        --no-kvm)   FORCE_TCG=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Build step
# ---------------------------------------------------------------------------
if [[ $DO_BUILD -eq 1 ]]; then
    echo "[run-qemu] Configuring CMake build in ${BUILD_DIR} ..."
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
          -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/x86_64-elf-clang.cmake" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DUNHOX_BOOT_TESTS=ON \
          --no-warn-unused-cli

    echo "[run-qemu] Building UNHOX (kernel + userspace) ..."
    cmake --build "${BUILD_DIR}"
fi

if [[ ! -f "${KERNEL_IMG}" ]]; then
    echo "[run-qemu] ERROR: kernel image not found at ${KERNEL_IMG}" >&2
    echo "[run-qemu] Hint: install prerequisites with: brew install llvm qemu" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Acceleration detection (macOS + Linux)
# ---------------------------------------------------------------------------
ACCEL_FLAGS=()
if [[ $FORCE_TCG -eq 0 ]]; then
    if [[ -w /dev/kvm ]] 2>/dev/null; then
        echo "[run-qemu] KVM available — using hardware acceleration"
        ACCEL_FLAGS=(-accel kvm -cpu host)
    elif [[ "$(uname)" == "Darwin" ]]; then
        # macOS: TCG for x86-64 guests on arm64 host
        # (HVF only works for matching architectures)
        echo "[run-qemu] macOS detected — using TCG (software emulation for x86-64)"
        ACCEL_FLAGS=(-accel tcg -cpu qemu64)
    else
        echo "[run-qemu] Using TCG (software emulation)"
        ACCEL_FLAGS=(-accel tcg -cpu qemu64)
    fi
else
    echo "[run-qemu] TCG forced — using software emulation"
    ACCEL_FLAGS=(-accel tcg -cpu qemu64)
fi

# ---------------------------------------------------------------------------
# Launch QEMU
# ---------------------------------------------------------------------------
echo "[run-qemu] Launching QEMU ..."
echo "[run-qemu] Serial output on stdio | GDB stub on localhost:1234"
echo "[run-qemu] Press Ctrl-A X to quit QEMU"
echo "-----------------------------------------------------------"

exec qemu-system-x86_64 \
    -machine q35 \
    "${ACCEL_FLAGS[@]}" \
    -m 256M \
    -kernel "${KERNEL_IMG}" \
    -initrd "${INITRD_IMG}" \
    -serial stdio \
    -display none \
    -no-reboot \
    -no-shutdown \
    -s
