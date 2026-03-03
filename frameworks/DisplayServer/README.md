# frameworks/DisplayServer/

DPS-inspired compositing display server — UNHOX original implementation.

## Overview

A Display PostScript-inspired display server for UNHOX.  Unlike X11 or Wayland,
this server uses Mach port IPC natively and exposes a PostScript-like drawing
model consistent with the NeXT heritage.

## Architecture

```
Client (Workspace Manager / AppKit)
    │
    │  Mach IPC (DPS_MSG_WINDOW_CREATE / DPS_MSG_DRAW_RECT / ...)
    ▼
Display Server (display_server_main)
    │  receives on: com.unhox.display
    │  registered with bootstrap server
    ▼
Compositor (VGA text mode 80×25 in Phase 5 v1)
    │
    ▼
VGA text buffer 0xB8000  ──►  Screen
```

## Files

| File | Description |
|------|-------------|
| `dps_msg.h` | Mach IPC message protocol (window create/destroy/draw/text) |
| `display_server.h` | Server API and compositor types |
| `display_server.c` | Server implementation (message loop + compositor) |
| `README.md` | This file |

## IPC Message Protocol

| ID  | Name                 | Direction     | Reply |
|-----|----------------------|---------------|-------|
| 400 | DPS_MSG_WINDOW_CREATE  | client → server | yes (wid) |
| 401 | DPS_MSG_WINDOW_DESTROY | client → server | no |
| 402 | DPS_MSG_WINDOW_MOVE    | client → server | no |
| 403 | DPS_MSG_WINDOW_RESIZE  | client → server | no |
| 404 | DPS_MSG_DRAW_RECT      | client → server | no |
| 405 | DPS_MSG_DRAW_TEXT      | client → server | no |
| 406 | DPS_MSG_FLUSH          | client → server | no |
| 407 | DPS_MSG_REPLY          | server → client | — |

## Phase 5 v1 Status

- [x] Mach IPC protocol design (`dps_msg.h`)
- [x] Compositor: VGA text mode 80×25 desktop background, menu bar, status bar
- [x] Window management: create, destroy, move, resize (up to 32 windows)
- [x] Drawing primitives: fill rect, draw text (VGA font approximation)
- [x] Bootstrap registration: "com.unhox.display"
- [x] AppKit backend adapter (`frameworks/AppKit/appkit_backend.c`)
- [ ] Full framebuffer (VESA/GOP) — requires GRUB VBE or UEFI GOP
- [ ] GPU acceleration path — Phase 6+

## Milestone v1.0

The display server is required for milestone v1.0:
> **"NeXT-heritage desktop boots"**

Serial output:
```
[display] display server starting
[display] compositor initialised (VGA text mode 80x25)
[display] registered as com.unhox.display
[display] display server ready
```

## References

- NeXTSTEP Display PostScript documentation (`archive/next-docs/`)
- GNUstep `back/` — existing backends (X11, Cairo, Windows)
- Adobe DPS Concepts (1991) — §2 Window architecture
