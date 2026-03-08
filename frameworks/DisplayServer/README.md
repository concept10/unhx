# frameworks/DisplayServer/

DPS-inspired compositing display server — NEOMACH original implementation.

## Overview

A new Display PostScript-inspired display server for NEOMACH. Unlike X11 or Wayland,
this server will use Mach port IPC natively and expose a PostScript-like drawing
model consistent with the NeXT heritage.

## Implementation Plan

- [ ] Mach IPC protocol design for display server
- [ ] Framebuffer initialization (via device server)
- [ ] Basic window management primitives
- [ ] AppKit backend adapter (GNUstep `back` port)
- [ ] GPU acceleration path (Phase 5)

## References

- NeXTSTEP Display PostScript documentation (`archive/next-docs/`)
- GNUstep `back/` — existing backends (X11, Cairo, Windows)
- Wayland protocol (IPC reference only)
