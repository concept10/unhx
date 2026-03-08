# Source Inventory & Licenses

Complete inventory of all external sources used in NEOMACH, with provenance and
licensing information.

## Kernel References (archive/)

| Source | Location | Origin | License |
|--------|----------|--------|---------|
| CMU Mach 3.0 | `archive/cmu-mach/` | Utah Flux / Bitsavers | CMU/MIT permissive |
| OSF MK6/MK7 | `archive/osf-mk/` | MkLinux archives | OSF/MIT permissive |
| Utah OSKit | `archive/utah-oskit/` | Utah Flux Project | Utah/MIT permissive |
| Lites (BSD server) | `archive/utah-oskit/lites/` | Utah Flux Project | BSD |
| NeXTSTEP docs | `archive/next-docs/` | Bitsavers, Archive.org | Historical reference |
| GNU Mach | `archive/gnu-mach-ref/` | Savannah | GPL-2.0 (reference only) |
| GNU Hurd | `archive/hurd-ref/` | Savannah | GPL-2.0 (reference only) |

### Notes on Kernel References

- **CMU Mach 3.0**: The original microkernel by Rashid, Tevanian et al.
  (1985–1994). Uses a very permissive CMU/MIT license that allows
  redistribution and modification. This is our primary design reference.

- **OSF MK**: The Open Software Foundation's continuation of Mach (MK6, MK7).
  Based on CMU Mach 3.0 with performance improvements. Same permissive license
  heritage.

- **GNU Mach / Hurd**: Included as *read-only design references*. These are
  GPL-2.0 licensed. No code is copied from these projects — they are consulted
  only for understanding Mach semantics and server architecture.

## Framework Submodules (frameworks/)

| Submodule | Path | Upstream | License |
|-----------|------|----------|---------|
| libobjc2 | `frameworks/objc-runtime/` | [gnustep/libobjc2](https://github.com/gnustep/libobjc2) | MIT |
| GNUstep Base | `frameworks/Foundation/` | [gnustep/libs-base](https://github.com/gnustep/libs-base) | LGPL-2.1 |
| GNUstep GUI | `frameworks/AppKit/` | [gnustep/libs-gui](https://github.com/gnustep/libs-gui) | LGPL-2.1 |
| libdispatch | `frameworks/libdispatch/` | [apple/swift-corelibs-libdispatch](https://github.com/apple/swift-corelibs-libdispatch) | Apache-2.0 |
| CoreFoundation | `frameworks/CoreFoundation/` | [apple/swift-corelibs-foundation](https://github.com/apple/swift-corelibs-foundation) | Apache-2.0 |

### Notes on Framework Licenses

- **libobjc2** (MIT): No restrictions on use in NEOMACH.
- **GNUstep Base/GUI** (LGPL-2.1): Must be dynamically linked or provide
  object files for relinking. NEOMACH servers will link these as shared libraries.
- **libdispatch / CoreFoundation** (Apache-2.0): Permissive. Must retain
  copyright notices and license text.

## NEOMACH Original Code

All original NEOMACH code (kernel, servers, build system, documentation) is
authored by the NEOMACH project contributors. License TBD — expected to be a
permissive license (MIT or BSD-2-Clause).

## XNU Reference (not mirrored)

Apple's XNU kernel is consulted as a design reference but not mirrored:
- Repository: https://github.com/apple-oss-distributions/xnu
- License: Apple Public Source License 2.0 (APSL-2.0)
- No code is copied from XNU.

## Obtaining Archives

Run `./tools/mirror-archives.sh --all` to download available reference sources.
See the script for manual download instructions for archives not available via
git.
