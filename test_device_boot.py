#!/usr/bin/env python3
import subprocess
import time
import os

print("[*] Starting QEMU...")
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
proc = subprocess.Popen(
    ['./tools/run-qemu.sh', '--no-build'],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    cwd=SCRIPT_DIR
)

start_time = time.time()
output_lines = []

try:
    while time.time() - start_time < 35:
        line = proc.stdout.readline()
        if not line:
            break
        output_lines.append(line)
        if "fork syscall" in line or "wait syscall" in line or "device" in line.lower():
            print(f">> {line.rstrip()}")
finally:
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except:
        proc.kill()

print("\n[*] Boot sequence complete. Device layer messages:")
for line in output_lines:
    if any(x in line.lower() for x in ['pci', 'virtio', 'device', 'initialising device']):
        print(f"   {line.rstrip()}")

