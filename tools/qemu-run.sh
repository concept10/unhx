#!/usr/bin/env bash
# tools/qemu-run.sh — Convenience alias for tools/run-qemu.sh
#
# This script exists for compatibility with TASKS.md naming.
# All logic lives in run-qemu.sh.

exec "$(dirname "${BASH_SOURCE[0]}")/run-qemu.sh" "$@"
