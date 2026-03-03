#!/usr/bin/env python3
"""
test_disk_io.py — Test virtio-blk disk I/O functionality

Creates a minimal disk image, boots QEMU with virtio-blk device,
and validates that read/write operations work.
"""

import subprocess
import time
import os
import sys

# Configuration
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
QEMU_BIN = "qemu-system-x86_64"
KERNEL_PATH = os.path.join(SCRIPT_DIR, "build", "kernel", "unhx.elf")
INITRD_PATH = os.path.join(SCRIPT_DIR, "build", "user", "init.elf")
DISK_IMAGE = "/tmp/unhx-test-disk.img"
DISK_SIZE_MB = 10

def create_disk_image():
    """Create a minimal disk image for testing."""
    # Remove old image if exists
    if os.path.exists(DISK_IMAGE):
        os.remove(DISK_IMAGE)
    
    # Create a simple disk image (sparse file)
    with open(DISK_IMAGE, 'wb') as f:
        f.seek(DISK_SIZE_MB * 1024 * 1024 - 1)
        f.write(b'\0')
    
    print(f"[test] Created disk image: {DISK_IMAGE} ({DISK_SIZE_MB}MB)")

def run_qemu_with_disk():
    """Boot QEMU with virtio-blk disk and capture output."""
    cmd = [
        QEMU_BIN,
        "-machine", "q35",
        "-accel", "tcg",
        "-cpu", "qemu64",
        "-m", "256M",
        "-kernel", KERNEL_PATH,
        "-initrd", INITRD_PATH,
        "-drive", f"file={DISK_IMAGE},if=virtio,format=raw",  # virtio-blk disk
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot",
        "-no-shutdown",
    ]
    
    print("[test] Starting QEMU with virtio-blk disk...")
    print(f"[test] Command: {' '.join(cmd)}")
    
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
        bufsize=1
    )
    
    output_lines = []
    start_time = time.time()
    timeout = 30  # 30 second timeout
    
    try:
        while time.time() - start_time < timeout:
            line = proc.stdout.readline()
            if not line:
                time.sleep(0.1)
                continue
            
            output_lines.append(line.strip())
            print(line.rstrip(), flush=True)
            
            # Check for key events
            if "[virtio-blk] device ready" in line:
                print("[test] ✓ virtio-blk device initialized")
            if "[virtio-blk] read complete" in line:
                print("[test] ✓ Read operation completed")
            if "[virtio-blk] write complete" in line:
                print("[test] ✓ Write operation completed")
            if "[init] PASS" in line:
                print(f"[test] ✓ Init test passed: {line}")
            if "[init] FAIL" in line:
                print(f"[test] ✗ Init test failed: {line}")
    
    except KeyboardInterrupt:
        print("\n[test] Interrupted by user")
    
    finally:
        proc.terminate()
        proc.wait(timeout=2)
    
    return output_lines

def analyze_output(output):
    """Analyze test output for success indicators."""
    output_text = '\n'.join(output)
    
    success_checks = {
        "PCI enumeration": "[pci] found" in output_text,
        "Device initialization": "[virtio-blk] device ready" in output_text,
        "Features negotiated": "[virtio-blk] host features" in output_text,
        "Read operation": "[virtio-blk] read" in output_text and "[virtio-blk] read complete" in output_text,
        "Write operation": "[virtio-blk] write" in output_text,
    }
    
    print("\n" + "="*60)
    print("TEST RESULTS")
    print("="*60)
    
    all_passed = True
    for check_name, passed in success_checks.items():
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"{status}: {check_name}")
        if not passed:
            all_passed = False
    
    print("="*60)
    
    # Check for errors
    if "error" in output_text.lower() or "failed" in output_text.lower():
        print("\nNote: Error messages detected in output (may be expected)")
    
    return all_passed

def main():
    if not os.path.exists(KERNEL_PATH) or not os.path.exists(INITRD_PATH):
        print("[error] Kernel or initrd not found. Run: cmake --build build")
        sys.exit(1)
    
    # Create test disk image
    create_disk_image()
    
    # Run QEMU
    output = run_qemu_with_disk()
    
    # Analyze results
    success = analyze_output(output)
    
    # Cleanup
    if os.path.exists(DISK_IMAGE):
        # Keep image for debugging, but can be removed
        print(f"\n[test] Disk image preserved: {DISK_IMAGE}")
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
