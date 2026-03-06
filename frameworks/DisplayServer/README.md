# frameworks/DisplayServer/

DPS-inspired compositing display server — UNHOX original implementation.

## Overview

A new Display PostScript-inspired display server for UNHOX. Unlike X11 or Wayland,
this server will use Mach port IPC natively and expose a PostScript-like drawing
model consistent with the NeXT heritage.

For a full comparison of display server architectures (macOS DPS/Quartz/Metal,
X11, Wayland, Windows DWM, BSD app_server) see `docs/display-server-survey.md`.

The formal design proposal and gate conditions are in
`docs/rfcs/RFC-0002-display-server-alternatives.md`.

## Implementation Plan

- [ ] L4 IPC gate conditions met (OOL descriptors, port transfer, blocking recv)
  — see `docs/rfcs/RFC-0002-display-server-alternatives.md` §Phase A
- [ ] Mach IPC protocol design for display server (message IDs 4000+)
- [ ] Framebuffer initialization (via device server)
- [ ] Basic window management primitives
- [ ] AppKit backend adapter (GNUstep `back` port)
- [ ] GPU acceleration path (Phase D)

## Relationship to `servers/display/`

`frameworks/DisplayServer/` contains the **framework layer** — the client
library (`libdisplay.a`) and high-level drawing API that applications use.

`servers/display/` contains the **server processes** — `unhx-display` (core),
X11 personality, and Wayland personality.

## References

- `docs/display-server-survey.md` — comparative survey of all major display server architectures
- `docs/rfcs/RFC-0002-display-server-alternatives.md` — design RFC
- NeXTSTEP Display PostScript documentation (`archive/next-docs/`)
- GNUstep `back/` — existing backends (X11, Cairo, Windows)
- Wayland protocol (IPC reference only)
