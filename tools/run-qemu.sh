#!/usr/bin/env bash
# tools/run-qemu.sh — Build UNHOX and launch it under QEMU
#
# Usage:
#   ./tools/run-qemu.sh [--arch=x86_64|aarch64] [--no-build] [--no-kvm]
#
#   --arch=x86_64   Build and run the x86-64 kernel (default)
#   --arch=aarch64  Build and run the AArch64 kernel (for Apple Silicon hosts)
#   --no-build  Skip the CMake build step (use an existing kernel image)
#   --no-kvm    Force TCG software emulation even if KVM is available
#
# Prerequisites (macOS arm64):
#   brew install llvm qemu
#
# QEMU flags explained (x86-64):
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
#
# QEMU flags explained (aarch64):
#   -machine virt       Generic AArch64 virtual board (GIC, PL011, virtio)
#   -cpu cortex-a57     ARMv8-A CPU emulation
#   -m 256M             256 MB RAM
#   -kernel <img>       Load ELF kernel directly
#   -serial stdio       PL011 UART output to stdout
#   -display none       Headless
#   -accel tcg          Software emulation (required for cross-arch guests)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="${REPO_ROOT}/.tmp"

# Create project-local temporary directory for QEMU files
mkdir -p "${TMP_DIR}"

ARCH="x86_64"
DO_BUILD=1
FORCE_TCG=0

for arg in "$@"; do
    case "$arg" in
        --arch=x86_64)  ARCH="x86_64" ;;
        --arch=aarch64) ARCH="aarch64" ;;
        --no-build) DO_BUILD=0 ;;
        --no-kvm)   FORCE_TCG=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

if [[ "$ARCH" == "aarch64" ]]; then
    BUILD_DIR="${REPO_ROOT}/build-aarch64"
    TOOLCHAIN="${REPO_ROOT}/cmake/aarch64-elf-clang.cmake"
else
    BUILD_DIR="${REPO_ROOT}/build"
    TOOLCHAIN="${REPO_ROOT}/cmake/x86_64-elf-clang.cmake"
fi

KERNEL_IMG="${BUILD_DIR}/unhx.elf"

# ---------------------------------------------------------------------------
# Build step
# ---------------------------------------------------------------------------
if [[ $DO_BUILD -eq 1 ]]; then
    echo "[run-qemu] Configuring CMake build in ${BUILD_DIR} (arch=${ARCH}) ..."
    cmake -S "${REPO_ROOT}/kernel" -B "${BUILD_DIR}" \
          -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DUNHOX_BOOT_TESTS=ON \
          --no-warn-unused-cli

    echo "[run-qemu] Building UNHOX kernel ..."
    cmake --build "${BUILD_DIR}" --target unhx.elf
fi

if [[ ! -f "${KERNEL_IMG}" ]]; then
    echo "[run-qemu] ERROR: kernel image not found at ${KERNEL_IMG}" >&2
    echo "[run-qemu] Hint: install prerequisites with: brew install llvm qemu" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Acceleration detection and QEMU launch
# ---------------------------------------------------------------------------
if [[ "$ARCH" == "aarch64" ]]; then
    # AArch64 always uses TCG (cross-arch guest — HVF/KVM not available)
    echo "[run-qemu] Launching QEMU (aarch64) ..."
    echo "[run-qemu] Serial output on stdio | GDB stub on localhost:1234"
    echo "[run-qemu] Press Ctrl-A X to quit QEMU"
    echo "-----------------------------------------------------------"
    exec qemu-system-aarch64 \
        -machine virt,virtualization=off \
        -cpu cortex-a57 \
        -accel tcg \
        -m 256M \
        -kernel "${KERNEL_IMG}" \
        -serial stdio \
        -display none \
        -no-reboot \
        -pidfile "${TMP_DIR}/qemu-aarch64.pid" \
        -s
fi

# x86-64 path below
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
# Launch QEMU (x86-64)
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
    -serial stdio \
    -display none \
    -no-reboot \
    -no-shutdown \
    -pidfile "${TMP_DIR}/qemu-x86_64.pid" \
    -s
