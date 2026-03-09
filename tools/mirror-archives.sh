#!/usr/bin/env bash
# tools/mirror-archives.sh — Download reference source archives for NEOMACH
#
# This script obtains historical Mach kernel sources and documentation
# used as design references for the NEOMACH project.
#
# Usage:
#   ./tools/mirror-archives.sh [--all | --cmu | --osf | --utah | --next | --gnu]
#
# Each archive is downloaded into the corresponding archive/ subdirectory.
# Existing files are not re-downloaded.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---------------------------------------------------------------------------
# CMU Mach 3.0 / Mach4
# ---------------------------------------------------------------------------
mirror_cmu() {
    local dest="${REPO_ROOT}/archive/cmu-mach"
    echo "[mirror] CMU Mach 3.0 → ${dest}"

    # Utah Flux Mach4 — the most accessible version of the CMU Mach source
    if [[ ! -d "${dest}/mach4" ]]; then
        echo "[mirror]   Cloning Utah Mach4 (derived from CMU Mach 3.0)..."
        git clone --depth 1 https://github.com/AlessandroSangiworgi/mach.git \
            "${dest}/mach4" 2>/dev/null || \
        echo "[mirror]   WARNING: CMU Mach4 clone failed."
        echo "[mirror]   Manual alternative: download from http://www.cs.utah.edu/flux/mach4/"
        echo "[mirror]   Or check https://bitsavers.org/bits/CMU/"
    else
        echo "[mirror]   mach4/ already present, skipping"
    fi
}

# ---------------------------------------------------------------------------
# OSF MK (MkLinux)
# ---------------------------------------------------------------------------
mirror_osf() {
    local dest="${REPO_ROOT}/archive/osf-mk"
    echo "[mirror] OSF MK → ${dest}"

    if [[ ! -d "${dest}/mklinux" ]]; then
        echo "[mirror]   Attempting MkLinux archive clone..."
        git clone --depth 1 https://github.com/AlessandroSangiworgi/mklinux.git \
            "${dest}/mklinux" 2>/dev/null || \
        echo "[mirror]   WARNING: MkLinux clone failed."
        echo "[mirror]   Manual alternative: search MkLinux archives or OSF/RI mirrors."
    else
        echo "[mirror]   mklinux/ already present, skipping"
    fi
}

# ---------------------------------------------------------------------------
# Utah OSKit + Lites
# ---------------------------------------------------------------------------
mirror_utah() {
    local dest="${REPO_ROOT}/archive/utah-oskit"
    echo "[mirror] Utah OSKit + Lites → ${dest}"

    if [[ ! -d "${dest}/oskit" ]]; then
        echo "[mirror]   NOTE: Utah OSKit is no longer hosted on a public git repo."
        echo "[mirror]   Source: http://www.cs.utah.edu/flux/oskit/"
        echo "[mirror]   Try: wget -r -np http://www.cs.utah.edu/flux/oskit/src/"
        echo "[mirror]   Also check: https://github.com/search?q=oskit+flux"
    else
        echo "[mirror]   oskit/ already present, skipping"
    fi

    if [[ ! -d "${dest}/lites" ]]; then
        echo "[mirror]   NOTE: Utah Lites (BSD server for Mach)"
        echo "[mirror]   Source: http://www.cs.utah.edu/flux/lites/"
        echo "[mirror]   Check Savannah for Hurd's lites variant."
    else
        echo "[mirror]   lites/ already present, skipping"
    fi
}

# ---------------------------------------------------------------------------
# NeXTSTEP / OPENSTEP documentation
# ---------------------------------------------------------------------------
mirror_next() {
    local dest="${REPO_ROOT}/archive/next-docs"
    echo "[mirror] NeXTSTEP/OPENSTEP docs → ${dest}"

    echo "[mirror]   Documentation PDFs available at:"
    echo "[mirror]     https://bitsavers.org/pdf/next/"
    echo "[mirror]     https://archive.org/search?query=NeXTSTEP"
    echo "[mirror]   Key documents:"
    echo "[mirror]     - NeXTSTEP Object-Oriented Programming and the Objective-C Language"
    echo "[mirror]     - OPENSTEP Specification"
    echo "[mirror]     - NeXTSTEP Operating System Software (developer docs)"

    if command -v wget &>/dev/null; then
        echo "[mirror]   To download bitsavers PDFs:"
        echo "[mirror]     wget -r -np -nd -P '${dest}/bitsavers/' https://bitsavers.org/pdf/next/"
    fi
}

# ---------------------------------------------------------------------------
# GNU Mach + Hurd (actively maintained Mach derivatives)
# ---------------------------------------------------------------------------
mirror_gnu() {
    echo "[mirror] GNU Mach + Hurd references"

    local gnu_mach="${REPO_ROOT}/archive/gnu-mach-ref"
    if [[ ! -d "${gnu_mach}" ]]; then
        echo "[mirror]   Cloning GNU Mach..."
        git clone --depth 1 https://git.savannah.gnu.org/git/hurd/gnumach.git \
            "${gnu_mach}" 2>&1
    else
        echo "[mirror]   gnu-mach-ref/ already present, skipping"
    fi

    local hurd="${REPO_ROOT}/archive/hurd-ref"
    if [[ ! -d "${hurd}" ]]; then
        echo "[mirror]   Cloning GNU Hurd..."
        git clone --depth 1 https://git.savannah.gnu.org/git/hurd/hurd.git \
            "${hurd}" 2>&1
    else
        echo "[mirror]   hurd-ref/ already present, skipping"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if [[ $# -eq 0 ]]; then
    echo "Usage: $0 [--all | --cmu | --osf | --utah | --next | --gnu]"
    echo ""
    echo "Downloads reference source archives for NEOMACH development."
    exit 0
fi

for arg in "$@"; do
    case "$arg" in
        --all)  mirror_cmu; mirror_osf; mirror_utah; mirror_next; mirror_gnu ;;
        --cmu)  mirror_cmu ;;
        --osf)  mirror_osf ;;
        --utah) mirror_utah ;;
        --next) mirror_next ;;
        --gnu)  mirror_gnu ;;
        *)      echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

echo ""
echo "[mirror] Done. Check archive/ for downloaded sources."
