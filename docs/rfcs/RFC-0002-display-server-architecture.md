# RFC-0002: UNHOX Display Server Architecture

- **Status**: Draft
- **Author**: UNHOX Project
- **Date**: 2026-03-06

## Summary

This RFC defines the architecture of the UNHOX Display Server — the Phase 5
desktop component responsible for compositing, input routing, and GPU surface
management. The design is informed by a comprehensive survey of existing display
servers (X11, Wayland, DPS, Quartz, SurfaceFlinger, Scenic/Flatland) and is
optimised for UNHOX's Mach microkernel IPC substrate.

See also: `docs/display-server-architectures.md` and
`docs/graphics-pipeline-microkernel.md` for the full design context.

## Motivation

UNHOX needs a display server that:

1. Is native to the Mach port IPC model — no foreign socket protocols
2. Supports composited windows with alpha from day one (no Xcomposite retrofit)
3. Provides a path to GPU acceleration via the device server
4. Is consistent with the NeXT/DPS heritage of the AppKit framework layer
5. Can evolve to support GPU compute (ML upscaling, ray tracing) as the GPU
   device server matures

Existing display servers (X11, Wayland) are designed for Unix IPC (sockets and
file descriptors). Adapting them to Mach IPC would require extensive shim layers.
A new, purpose-built server following DPS/NeXTSTEP conventions is the right
approach for UNHOX.

## Architecture

### Component Hierarchy

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Application Task (AppKit + Objective-C)                                │
│    NSWindow, NSView, NSEvent                                            │
│    └── UNHOXDisplayContext (DPS-inspired drawing context)               │
│         └── mach_msg() → display_server_port                           │
├─────────────────────────────────────────────────────────────────────────┤
│  Display Server Task (frameworks/DisplayServer/)                        │
│    ├── Window registry (window ID → surface + client port)             │
│    ├── Compositor (software Phase 5; Vulkan Phase 6+)                  │
│    ├── Input router (keyboard, pointer events → focused window)        │
│    └── IPC: display_server_port (well-known bootstrap port name)       │
├─────────────────────────────────────────────────────────────────────────┤
│  GPU Device Server (servers/device/gpu/)                               │
│    ├── Vulkan device management                                        │
│    ├── GPU VA space per client                                         │
│    └── Command buffer submit / completion notification                 │
├─────────────────────────────────────────────────────────────────────────┤
│  UNHOX Mach Kernel (IPC, VM, Tasks)                                    │
└─────────────────────────────────────────────────────────────────────────┘
```

### IPC Protocol

The display server registers with the bootstrap server under the name
`"com.unhox.display"`. Clients look up this port at startup.

#### Message IDs (msgh_id)

| ID  | Operation | Direction |
|-----|-----------|-----------|
| 100 | `DS_CREATE_WINDOW` | Client → Server |
| 101 | `DS_DESTROY_WINDOW` | Client → Server |
| 102 | `DS_SET_TITLE` | Client → Server |
| 103 | `DS_ATTACH_SURFACE` | Client → Server |
| 104 | `DS_PRESENT_SURFACE` | Client → Server |
| 110 | `DS_KEY_EVENT` | Server → Client |
| 111 | `DS_POINTER_EVENT` | Server → Client |
| 112 | `DS_EXPOSE_EVENT` | Server → Client |
| 113 | `DS_CLOSE_EVENT` | Server → Client |

#### `DS_CREATE_WINDOW` Request

```c
struct ds_create_window_req {
    mach_msg_header_t  header;       /* msgh_id = DS_CREATE_WINDOW         */
    uint32_t           width;
    uint32_t           height;
    uint32_t           flags;        /* DS_WIN_TITLED | DS_WIN_CLOSABLE ... */
    mach_port_name_t   event_port;   /* reply port for events              */
};

struct ds_create_window_reply {
    mach_msg_header_t  header;
    kern_return_t      result;
    uint32_t           window_id;
};
```

#### `DS_ATTACH_SURFACE` / `DS_PRESENT_SURFACE`

```c
struct ds_attach_surface_req {
    mach_msg_header_t        header;    /* msgh_id = DS_ATTACH_SURFACE     */
    mach_msg_ool_descriptor_t surface;  /* OOL: GPU or software surface    */
    uint32_t                 window_id;
    uint32_t                 width;
    uint32_t                 height;
    uint32_t                 format;    /* DS_FMT_BGRA8888, DS_FMT_RGB565  */
    uint64_t                 gpu_fence; /* 0 = software surface (no fence) */
};
```

The OOL memory descriptor carries a Mach memory entry mapping the surface
buffer. For software rendering (Phase 5), this is a regular memory-mapped
buffer. For GPU rendering (Phase 6+), it is a GPU-accessible buffer shared
via the GPU device server's memory allocation interface.

### Compositor Design

**Phase 5 (software):**

- Compositor maintains a sorted list of window surfaces (z-order)
- On `DS_PRESENT_SURFACE`, the compositor redraws dirty regions using
  Porter-Duff `over` compositing in software
- Output is written to the framebuffer via the device server (DRM/KMS or
  virtio-gpu in QEMU)

**Phase 6+ (Vulkan):**

- Each client surface is a Vulkan image (allocated via GPU device server)
- Compositor runs a Vulkan graphics pass:
  - Input: per-window Vulkan images + transform matrices
  - Output: scanout image (presented via `VK_KHR_display`)
- GPU fence synchronisation ensures client rendering completes before composite

**Phase 7+ (AI upscaling):**

- Optional ML upscaling pass (ONNX Runtime / Vulkan EP):
  - Render at half resolution in client
  - Upscale to native resolution in compositor using DLSS/FSR-style model
  - Reduces per-client GPU load; amortised across all windows

### Input Routing

The display server receives raw input events from the input device server
(`servers/device/input/`). It maintains:

- A pointer position + focused window
- A keyboard focus window
- Per-window grab state

Events are delivered to the focused window's `event_port` (a Mach send right
held by the display server, provided by the client at window creation).

```
Input Device Server
  └── mach_msg(SEND, display_server_input_port, raw_event)
       ↓
Display Server
  └── hit-test → focused window
  └── mach_msg(SEND, window_event_port, translated_event)
       ↓
Application Task
  └── NSRunLoop receives event → NSWindow → NSView → responder chain
```

### DPS Drawing Model

For Phase 5, clients draw using a PostScript-inspired command set:

```
DPSNewContext()   → creates a drawing context backed by an off-screen surface
DPSSetFont()      → select font (name, size, transform)
DPSMoveto(x, y)   → move current point
DPSLineto(x, y)   → line to point
DPSStroke()       → stroke current path
DPSFill()         → fill current path
DPSSetColor(r, g, b, a) → set current colour (with alpha)
DPSCompositeImage(src, dstRect, op) → composite an image
DPSFlush()        → submit drawing commands → DS_PRESENT_SURFACE
```

These map to:
- Phase 5: Cairo / Pixman software rendering
- Phase 6+: Vulkan fragment shader rendering (Metal-style path rendering)
- Phase 7+: GPU path rendering via Vulkan compute (like NanoVG-Metal)

## Alternatives Considered

### Use Wayland Protocol

Wayland's protocol is well-specified and has a broad ecosystem of client
implementations. However:

- Wayland uses Unix sockets; UNHOX IPC is Mach ports. Bridging the two adds
  a protocol translation layer with non-trivial overhead and complexity.
- Wayland's `wl_buffer` / dma-buf model requires Linux-specific file descriptors;
  the equivalent in UNHOX is Mach memory entries (different type system).
- The NeXT DPS heritage of AppKit expects a DPS-compatible drawing model, not
  a Wayland `wl_surface`.

XWayland-style compatibility could be provided later to run Wayland clients.

### Use X11 Protocol

X11 is the most widely supported protocol but has the most baggage: compositing
is retrofitted, security is a shared domain, and the protocol is frozen in 1987.
Not appropriate for a new microkernel OS.

### Embed Compositor in Kernel

Plan 9 embeds display in `/dev/draw` (a kernel device). This eliminates IPC
overhead but violates microkernel discipline: a display bug could crash the
kernel. Not appropriate for UNHOX.

## Implementation Plan

- [ ] `frameworks/DisplayServer/include/display_msg.h` — message type definitions
- [ ] `frameworks/DisplayServer/server/ds_main.c` — server event loop
- [ ] `frameworks/DisplayServer/server/ds_window.c` — window registry
- [ ] `frameworks/DisplayServer/server/ds_compositor.c` — software compositor
- [ ] `frameworks/DisplayServer/server/ds_input.c` — input routing
- [ ] `frameworks/DisplayServer/client/libdisplay.c` — client-side library
- [ ] `frameworks/AppKit/backend/unhox/` — AppKit UNHOX backend

## Open Questions

1. **Window decorations:** Client-side (application draws title bar) or
   server-side (display server draws decorations)? NeXTSTEP was server-side;
   Wayland prefers client-side. UNHOX will start server-side for simplicity.

2. **VSync:** How does the display server signal clients to render a new frame?
   Options: (a) async Mach message per-vsync; (b) shared memory frame counter;
   (c) presentation feedback (like Wayland `wp-presentation`). Option (a) is
   simplest and consistent with Mach IPC model.

3. **Multi-monitor:** The protocol uses a single logical screen in Phase 5.
   Multi-monitor support (separate compositor instances or a unified scene
   graph) is deferred to Phase 6.

## References

- `docs/display-server-architectures.md` — full display server survey
- `docs/graphics-pipeline-microkernel.md` — GPU pipeline and AI/ML
- `docs/rfcs/RFC-0001-ipc-message-format.md` — Mach IPC message format
- NeXTSTEP Display PostScript System Reference (`archive/next-docs/`)
- Adobe, "Programming the Display PostScript System" (1992)
- Fuchsia Scenic/Flatland design docs — fuchsia.dev
- Wayland Protocol Specification — https://gitlab.freedesktop.org/wayland/wayland
