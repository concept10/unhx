# frameworks/DisplayServer/

DPS-inspired compositing display server — UNHOX original implementation.

## Overview

A new Display PostScript-inspired display server for UNHOX. Unlike X11 or Wayland,
this server will use Mach port IPC natively and expose a PostScript-like drawing
model consistent with the NeXT heritage.

The design is documented in full in `docs/rfcs/RFC-0002-display-server-architecture.md`.
For a comprehensive survey of existing display server architectures (X11, Wayland,
DPS, Quartz, SurfaceFlinger, Scenic/Flatland, DirectX/WDDM) that informed this
design, see `docs/display-server-architectures.md`.

For GPU pipeline advances (shaders, ray tracing, AI/ML inference) and their
integration with UNHOX's microkernel IPC model, see
`docs/graphics-pipeline-microkernel.md`.

## Architecture

```
Application (AppKit)
  └── DPS drawing context (libdisplay client)
       └── mach_msg() → display_server_port (Mach IPC)
            ↓
Display Server (this directory)
  ├── Window registry
  ├── Compositor (software → Vulkan Phase 6+)
  └── Input router
       ↓
GPU Device Server (servers/device/gpu/)  ←→  DRM/KMS / virtio-gpu
```

## Implementation Plan

- [ ] `include/display_msg.h` — Mach IPC message type definitions (RFC-0002)
- [ ] `server/ds_main.c` — server event loop (mach_msg receive dispatch)
- [ ] `server/ds_window.c` — window registry (create, destroy, z-order)
- [ ] `server/ds_compositor.c` — software compositor (Porter-Duff compositing)
- [ ] `server/ds_input.c` — input event routing (keyboard, pointer)
- [ ] `client/libdisplay.c` — client-side library (DS_CREATE_WINDOW etc.)
- [ ] AppKit backend adapter (`frameworks/AppKit/backend/unhox/`)
- [ ] GPU acceleration path (Phase 6 — Vulkan compositor)
- [ ] AI/ML upscaling pass (Phase 7 — ONNX Runtime / Vulkan EP)

## References

- `docs/rfcs/RFC-0002-display-server-architecture.md` — UNHOX display server RFC
- `docs/display-server-architectures.md` — comprehensive architecture survey
- `docs/graphics-pipeline-microkernel.md` — GPU pipeline and AI/ML in microkernels
- NeXTSTEP Display PostScript documentation (`archive/next-docs/`)
- GNUstep `back/` — existing backends (X11, Cairo, Windows)
- Wayland protocol (IPC reference) — https://gitlab.freedesktop.org/wayland/wayland
- Fuchsia Scenic/Flatland — fuchsia.dev (closest structural analogue to UNHOX)
