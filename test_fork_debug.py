#!/usr/bin/env python3
import subprocess
import time

proc = subprocess.Popen(
    ['./tools/run-qemu.sh', '--no-build'],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    cwd='/Users/tracey/Developer/unhx'
)

try:
    time.sleep(45)
finally:
    proc.terminate()
    try:
        stdout, _ = proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, _ = proc.communicate()

# Print last 300 lines to see fork+wait output
lines = stdout.split('\n')
for line in lines[-300:]:
    if line.strip():
        print(line)
