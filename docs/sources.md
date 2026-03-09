# Source Inventory & Licenses

Complete inventory of all external sources used in Neomach, with provenance and
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

- **libobjc2** (MIT): No restrictions on use in Neomach.
- **GNUstep Base/GUI** (LGPL-2.1): Must be dynamically linked or provide
  object files for relinking. Neomach servers will link these as shared libraries.
- **libdispatch / CoreFoundation** (Apache-2.0): Permissive. Must retain
  copyright notices and license text.

## Audio Subsystem References (not mirrored)

The following external specifications and documentation are referenced by the
audio subsystem design (`docs/audio-server-design.md`, RFC-0005) but are not
included in the repository.

| Reference | URL | License |
|-----------|-----|---------|
| Intel HDA Specification Rev 1.0a | Intel developer site | Public specification |
| USB Audio Class 2.0 specification | usb.org | Public specification |
| USB MIDI Class 1.0 specification | usb.org | Public specification |
| virtio-snd specification | virtio-spec §5.14 | Apache-2.0 / public |
| Apple Core Audio Overview (2004) | developer.apple.com | Reference only |
| Apple Audio Unit Programming Guide (AU v2) | developer.apple.com | Reference only |
| Apple Audio Unit Extensions — AUv3 (2015) | developer.apple.com | Reference only |
| Apple Core MIDI Framework Reference | developer.apple.com | Reference only |
| Steinberg VST2 SDK 2.4 | Steinberg developer portal | Steinberg Free SDK (reference) |
| Steinberg VST3 SDK | https://github.com/steinbergmedia/vst3sdk | GPL v3 / Steinberg dual |
| CLAP SDK | https://github.com/free-audio/clap | MIT |
| LV2 Core Specification | https://lv2plug.in | ISC |

No code from Apple's Core Audio, Core MIDI, or Audio Unit implementations is
copied into Neomach.  These documents are consulted only for API design
inspiration.  The Neomach Audio Server, MIDI Server, and Audio Units framework
are original implementations using Mach IPC as their sole transport.

The Steinberg VST2 SDK is referenced for the frozen `AEffect` struct ABI only;
no VST2 SDK source is embedded.  The Steinberg VST3 SDK is an optional build
dependency for the VST3 bridge component and is fetched at build time under its
GPL v3 licence.  CLAP and LV2 SDKs are MIT and ISC respectively and may be
included as build dependencies.

## Neomach Original Code

All original Neomach code (kernel, servers, build system, documentation) is
authored by the Neomach project contributors. License TBD — expected to be a
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
