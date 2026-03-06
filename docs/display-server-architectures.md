# Display Server Architectures — A Comprehensive Survey

**Relevance to UNHOX:** UNHOX will implement a Display PostScript-inspired display
server (Phase 5) on top of Mach IPC. Understanding every major display architecture —
where each succeeded, where each failed, and why — directly informs the UNHOX design.

---

## Table of Contents

1. [What Is a Display Server?](#1-what-is-a-display-server)
2. [X Window System (X11 / X.Org)](#2-x-window-system-x11--xorg)
3. [Wayland and Weston](#3-wayland-and-weston)
4. [Display PostScript and NeXTSTEP (UNHOX Heritage)](#4-display-postscript-and-nextstep-unhox-heritage)
5. [macOS Quartz Compositor and Metal](#5-macos-quartz-compositor-and-metal)
6. [Android SurfaceFlinger / Gralloc](#6-android-surfaceflinger--gralloc)
7. [Windows: WDDM, DXGI, DirectComposition](#7-windows-wddm-dxgi-directcomposition)
8. [OpenGL and the Mesa / DRI Stack](#8-opengl-and-the-mesa--dri-stack)
9. [Vulkan and WSI](#9-vulkan-and-wsi)
10. [CUDA and GPU Compute (NVIDIA)](#10-cuda-and-gpu-compute-nvidia)
11. [ROCm and AMDGPU (AMD)](#11-rocm-and-amdgpu-amd)
12. [DirectX Family (Microsoft)](#12-directx-family-microsoft)
13. [Alternative and Research Architectures](#13-alternative-and-research-architectures)
14. [Comparative Summary](#14-comparative-summary)
15. [References](#15-references)

---

## 1. What Is a Display Server?

A display server is the component that arbitrates access to the GPU and screen
between competing clients (applications). It owns:

- The framebuffer or scanout surface
- Input event routing (keyboard, pointer, touch)
- Surface compositing (z-ordering, transparency, transforms)
- (Optionally) the rendering API surface — or that is delegated to a GPU driver

The canonical interaction model is:

```
Application ──IPC──> Display Server ──DRM/KMS──> GPU ──> Monitor
     ^                    │
     │                    └──> Input devices
     └──────────────────────────────────────────
```

The "IPC" link is where architecture choices matter most, and where a microkernel
like UNHOX can offer structural advantages.

---

## 2. X Window System (X11 / X.Org)

### Origins

X11 was designed at MIT in 1984 for a heterogeneous, networked workstation
environment. Its founding principle is *network transparency*: an application
running on machine A can render windows on display B over TCP.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Application (Xlib / XCB)                                   │
│    │                                                         │
│    │  X11 protocol (TCP socket or Unix domain socket)        │
│    ▼                                                         │
│  X Server (X.Org)                                           │
│    ├── Window Manager (separate process via EWMH)            │
│    ├── Compositor (Xcomposite/Xdamage + compton/picom)       │
│    ├── DRM/KMS (kernel mode-setting)                         │
│    └── OpenGL via GLX extension                              │
└─────────────────────────────────────────────────────────────┘
```

The X server owns the display exclusively and serialises all rendering through
its protocol. Window managers are *just another client* that happens to receive
SubstructureRedirect events.

### Protocol

The X11 wire protocol is a request/response binary protocol with ~200 core
requests, plus extensions. Each request is a fixed-size header followed by
variable-length data. The core protocol dates from 1987 and has not changed;
extensions (GLX, Xcomposite, XRandR, XInput2, etc.) add new request ranges.

Key extensions relevant to compositing:
- **Xcomposite** — redirects window rendering to off-screen pixmaps
- **Xdamage** — notifies compositor when a region has changed
- **XRandR** — runtime display reconfiguration
- **XSync** — synchronisation fences
- **DRI2 / DRI3** — direct rendering interface (bypasses X server for GPU work)

### Strengths

- Mature: 40+ years of applications, toolkits, and infrastructure
- Network transparency (useful for remote desktop, VNC alternatives)
- Highly extensible
- Every Unix-like system ships an X server

### Weaknesses

- **Architecture mismatch with compositing:** X was designed before GPU compositing.
  DRI3 + XComposite retrofits compositing, but requires round-trips through the
  X server to notify the compositor, adding latency.
- **Input latency:** Input events must traverse the X server before reaching the
  compositor and then the application.
- **Security:** All X clients share a single security domain. Any X client can
  intercept keystrokes from any window (`XGrabKey`, `XQueryTree`).
- **Privilege:** The X server runs as root or with elevated privileges for DRM.
- **Protocol overhead:** For local display, TCP/Unix socket round-trips add
  latency; Wayland eliminates most of these.
- **Extension sprawl:** Each new capability required a new extension, with its own
  versioning, negotiation, and quirks.

### Relation to UNHOX

X11's Unix-domain socket is conceptually similar to Mach port IPC — both are
local IPC channels. However, Mach messages have explicit typed rights and zero-copy
semantics (OOL memory) that X11 lacks. UNHOX's display server should learn from
X11's compositing retrofits and design compositing as a first-class citizen.

---

## 3. Wayland and Weston

### Origins

Kristian Høgsberg's 2008 paper and prototype described Wayland as an IPC protocol
between clients and a *compositor*, eliminating the X server as an intermediary.
Weston is the reference compositor implementation.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Application                                                 │
│    │  Wayland protocol (Unix domain socket)                  │
│    ▼                                                         │
│  Wayland Compositor (e.g. Weston, Mutter, KWin)             │
│    ├── Rendering its own UI directly via EGL/GL or Vulkan    │
│    ├── DRM/KMS (direct scanout)                              │
│    └── libinput (input event routing)                        │
│                                                              │
│  Client renders into a wl_buffer (backed by dmabuf/shm)      │
│  Compositor scans out / composites buffers directly          │
└─────────────────────────────────────────────────────────────┘
```

The key insight: the compositor *is* the display server. There is no separate
X server. The client renders into a GPU buffer and hands the buffer to the
compositor; the compositor decides when and how to display it.

### Protocol Design

Wayland uses a simple typed binary protocol over a Unix domain socket.
Objects are identified by numeric IDs; methods are dispatched as events or
requests. The protocol is versioned per-interface.

Core interfaces:
- `wl_display` — connection management
- `wl_compositor` — surface factory
- `wl_surface` — a rectangular region with a buffer
- `wl_seat` — input device group (keyboard + pointer + touch)
- `xdg_surface` / `xdg_toplevel` — desktop window semantics (via XDG Shell)

### Extensions (Wayland Protocols)

- **xdg-shell** — standard desktop window decorations
- **linux-dmabuf-unstable** — zero-copy GPU buffer sharing
- **wp-presentation** — frame presentation timing feedback
- **zwp-linux-explicit-sync** — GPU timeline/fence synchronisation
- **wp-drm-lease** — VR headset scanout

### Strengths

- Minimal round-trips: client renders, attaches buffer, compositor composites
- No global input grab security hole (each surface owns its own input)
- Direct GPU buffer sharing via dmabuf (zero-copy from app to compositor)
- Clean slate: no 40-year legacy

### Weaknesses

- Fractured ecosystem: X11 compatibility requires XWayland (a full X server)
- No network transparency (by design; VNC/RDP handle remote)
- XDG Shell took years to stabilise; many compositors diverged
- Explicit sync only recently standardised; tearing artifacts in early compositors
- Per-compositor extensions fragment the protocol surface

### Relation to UNHOX

Wayland's *compositor-as-server* model maps well to UNHOX's server architecture.
However, Wayland uses Unix sockets; UNHOX uses Mach port IPC. The structural
equivalent would be:

```
Application task
  │  mach_msg(SEND, display_server_port, surface_buffer_msg)
  ▼
Display Server task (Mach server)
  │  mach_msg(RECEIVE) → composites buffer → DRM/KMS
  ▼
GPU/Framebuffer Device Server
```

Wayland's dmabuf extension is equivalent to Mach OOL memory descriptors:
the kernel maps the same physical pages into both the client and compositor
without copying, using VM rights.

---

## 4. Display PostScript and NeXTSTEP (UNHOX Heritage)

### Origins

Adobe Display PostScript (DPS) was licensed to NeXT in 1988 as the rendering
model for NeXTSTEP 1.0. It replaced the fixed raster model of X11 with a
*retained, structured* drawing model based on PostScript.

### Architecture

```
NeXTSTEP Window Server (WindowServer process)
  ├── PostScript interpreter (Adobe DPS)
  ├── Compositing engine (alpha channel, Porter-Duff operators)
  ├── Event server (typed events: mouse, keyboard)
  └── Mach IPC interface (clients use Mach messages, not X protocol)

Application (AppKit)
  │  Mach IPC → WindowServer port
  │  NSDPSContext — PostScript drawing context
  └── wraps: PSxxx() calls → DPS opcodes → display
```

### Why This Matters for UNHOX

UNHOX's `frameworks/DisplayServer/` is explicitly DPS-inspired. Key design
decisions from NeXTSTEP that should carry forward:

1. **Mach IPC as the protocol** — no Unix socket; everything is a Mach message
   sent to a well-known port. This provides: typed rights transfer, zero-copy
   OOL memory, capability-based security.

2. **Structured drawing model** — rather than raw pixel operations, clients
   submit drawing commands (paths, transforms, images). The server can
   accelerate these on the GPU.

3. **Alpha compositing** — NeXTSTEP had per-window alpha before macOS made it
   famous. This requires the compositing engine to hold off-screen surfaces for
   each window.

4. **Single-process display authority** — the window server process owns all
   display state. Client crashes cannot corrupt the display.

### Display PostScript vs X11

| Feature | X11 | DPS/NeXTSTEP |
|---------|-----|--------------|
| Protocol | X11 binary protocol | Mach IPC |
| Drawing model | Raster (XDrawLine, etc.) | PostScript operators |
| Compositing | Bolted on (Xcomposite) | Native (alpha channels) |
| Security | Shared domain | Per-window rights |
| Network | Yes (TCP) | Mach IPC only (local) |

---

## 5. macOS Quartz Compositor and Metal

### Architecture

macOS replaced Display PostScript with Quartz (PDF-based) in 10.0, and added
Core Animation in 10.5 (Leopard) as a GPU-backed layer tree:

```
Application (AppKit / SwiftUI)
  └── Core Animation layer tree
       └── CARenderer → Metal command buffer → GPU
            └── WindowServer (Quartz Compositor)
                 └── IOGFX / IOKit → DRM/KMS
```

`WindowServer` on macOS is a privileged process that composites all surfaces
using Metal compute/fragment shaders and hands the final scanout buffer to the
display engine.

### Metal

Metal is Apple's low-overhead GPU API (2014), replacing OpenGL for Apple
platforms. Key characteristics:

- Explicit command buffer recording (no hidden driver state)
- CPU-side metal shader compilation (no runtime GLSL compilation)
- Explicit memory management (managed, shared, private heaps)
- Argument buffers — GPU pointers to GPU resources (reduces CPU–GPU sync)

Metal's display path: `CAMetalLayer` provides a `MTLDrawable` per frame; the
app renders into it and presents; `WindowServer` composites all layer drawables.

### Relevance to UNHOX

UNHOX Phase 5 will prototype a DPS-inspired server. macOS shows the evolution
path: DPS → Quartz PDF → Core Animation + Metal. UNHOX should design the server
to support a similar upgrade path, with the initial DPS-inspired layer being
replaceable by a GPU-accelerated compositor (Phase 5+).

---

## 6. Android SurfaceFlinger / Gralloc

Android's display stack is a useful reference for an embedded/mobile microkernel:

```
Application (Canvas / OpenGL ES / Vulkan)
  └── Surface (backed by BufferQueue)
       └── SurfaceFlinger (system service)
            ├── HWComposer (hardware compositor API)
            └── Gralloc (GPU buffer allocator/importer)
```

**BufferQueue:** A producer/consumer queue of GPU buffers. The app is the
producer; SurfaceFlinger is the consumer. Buffers are shared via file descriptors
(dma-buf) without copying.

**HWComposer HAL:** Allows the hardware display engine to composite layers
directly (bypassing the GPU) using hardware overlay planes — zero GPU cost for
compositing when layers are simple.

**Relevance to UNHOX:** The BufferQueue model maps to Mach port message queues
with OOL memory descriptors. HWComposer's overlay model shows how to offload
compositing to display hardware, reducing GPU load and power.

---

## 7. Windows: WDDM, DXGI, DirectComposition

### WDDM (Windows Display Driver Model, Vista+)

Pre-Vista (XPDM), the display driver ran in kernel mode and could blue-screen
the OS. WDDM moved display drivers to user mode, with a thin kernel stub:

```
Application
  └── D3D / DXGI
       └── User-mode display driver (UMD, in process)
            └── Kernel-mode display driver (KMD) — minimal
                 └── DirectX graphics kernel (dxgkrnl.sys)
                      └── Hardware
```

The UMD does most work in-process (command buffer recording). The KMD only
submits the pre-built command buffer to the GPU's DMA engine.

### DirectComposition

`DirectComposition` (Windows 8+) is the system compositor API. Applications
submit visual trees (transforms, effects, surfaces); DWM (Desktop Window Manager)
composites them using Direct3D, then scans out via DXGI.

### Relevance to UNHOX

WDDM's user-mode driver model is structurally similar to a microkernel's
device server: the "driver" lives in user space; only a thin kernel interface
is needed for DMA and interrupt handling. UNHOX's device server should follow
the same principle for GPU access.

---

## 8. OpenGL and the Mesa / DRI Stack

### The Stack on Linux

```
Application (OpenGL calls)
  └── libGL / libGLX → Mesa (user-space GL driver)
       ├── Gallium3D state tracker (OpenGL, OpenCL, etc.)
       ├── Hardware driver (radeonsi, nouveau, i915, etc.)
       └── DRI (Direct Rendering Infrastructure)
            ├── DRM (kernel: amdgpu.ko, i915.ko, nouveau.ko)
            └── GEM (Graphics Execution Manager) — GPU memory
```

**DRI3 + Present:** Direct Rendering Interface 3 allows the application to
allocate GPU buffers itself (via GBM or EGL), render, and then share the buffer
with the compositor (X11 or Wayland) via a file descriptor.

### OpenGL vs Vulkan

OpenGL has decades of driver-side state management. The driver must track
thousands of state variables and implicitly synchronise the GPU. This makes
drivers large, complex, and a source of bugs and inconsistency.

Vulkan eliminates most implicit state in favour of explicit descriptor sets,
pipeline objects, and synchronisation barriers.

### Relevance to UNHOX

Mesa is open-source and portable. For UNHOX Phase 5, Mesa (targeting a
`gallium-softpipe` or `llvmpipe` fallback first, then hardware drivers via DRM)
is the most pragmatic OpenGL/Vulkan path. The DRM kernel interface would live in
UNHOX's device server.

---

## 9. Vulkan and WSI

### Design Philosophy

Vulkan (2016, Khronos) is a ground-up redesign of OpenGL:

- **Explicit GPU control:** command buffer recording, pipeline state objects,
  explicit memory types, explicit synchronisation (semaphores, fences, events)
- **Multi-threading:** command recording is fully parallel; no single GL context lock
- **Predictable:** no hidden recompilation, no driver-side state tracking
- **Portable:** maps directly to D3D12, Metal command model; same shader language (SPIR-V)

### WSI (Window System Integration)

Vulkan has standardised extensions for each platform:
- `VK_KHR_xcb_surface` / `VK_KHR_xlib_surface` — X11
- `VK_KHR_wayland_surface` — Wayland
- `VK_KHR_win32_surface` — Windows
- `VK_KHR_display` — headless or direct scanout (no display server)
- `VK_EXT_headless_surface` — off-screen only

`VK_KHR_swapchain` provides the present/acquire loop; the application acquires
a framebuffer image, renders, and presents it back to the WSI layer.

### Vulkan Direct Display (`VK_KHR_display`)

This extension lets a Vulkan application take ownership of a display directly,
bypassing any display server. This is relevant for UNHOX's GPU acceleration path:
the display server itself can use `VK_KHR_display` to own the scanout surface.

### Explicit Synchronisation

`VK_EXT_external_memory_dma_buf` + `VK_EXT_image_drm_format_modifier` +
`VK_EXT_external_semaphore_fd` compose the Vulkan–Linux display server
integration: the compositor and application share GPU buffers (dma-buf) and
synchronisation (sync_file FDs / timeline semaphores).

### Relevance to UNHOX

For UNHOX Phase 5 GPU acceleration, Vulkan is the preferred rendering API:

1. The device server manages DRM/KMS and exposes a Mach port interface
2. The display server acquires DRM/KMS ownership via the device server
3. The display server uses `VK_KHR_display` to drive the scanout
4. Clients render into Vulkan images; OOL Mach messages carry the dma-buf handle
5. The compositor uses a Vulkan graphics pass to composite all client surfaces

---

## 10. CUDA and GPU Compute (NVIDIA)

CUDA (Compute Unified Device Architecture, 2007) is NVIDIA's proprietary parallel
compute platform. It is not a display server, but it is architecturally important
because NVIDIA GPUs use CUDA for:

- AI/ML inference (TensorRT, cuDNN)
- DLSS (AI upscaling — a neural network running on Tensor Cores)
- OptiX (hardware ray tracing API over CUDA)
- Video encode/decode (NVENC/NVDEC)

### CUDA in a Microkernel Context

CUDA's driver model: a user-mode library (`libcuda.so`) submits work to the
NVIDIA kernel-mode driver (`nvidia.ko`), which programs the GPU's DMA engine.

In a microkernel like UNHOX, the NVIDIA driver would live in the device server
(or a dedicated GPU server). The challenge is that NVIDIA's closed-source driver
is tightly coupled to the Linux kernel's internal APIs.

For UNHOX, the pragmatic path is open-source GPU drivers:
- **Mesa + RADV** (AMD Vulkan) or **Mesa + ANV** (Intel Vulkan) in the device server
- CUDA compatibility via CUDA-on-Vulkan (e.g. `clspv`, `zluda`)

---

## 11. ROCm and AMDGPU (AMD)

AMD's open-source GPU compute stack:

```
ROCm (Radeon Open Compute)
  ├── HIP — CUDA-compatible compute API
  ├── rocBLAS / MIOpen — ML primitives
  ├── LLVM AMDGPU backend — shader / kernel compilation
  └── amdgpu.ko — kernel-mode DRM driver
```

### AMDGPU Architecture

AMD GPUs use a command processor (CP) that reads command buffers (PM4 packets)
from a ring buffer in GPU-accessible memory. The DRM driver (in the kernel)
allocates the ring, maps GPU memory, and submits command buffer IBs (indirect
buffers) via IOCTL.

For UNHOX, `amdgpu.ko` would be the kernel stub; the user-mode Mesa/RADV driver
runs in the GPU device server. IPC between the display server and the GPU server
uses Mach messages with OOL descriptors for command buffers and surface handles.

### Relevance to UNHOX

AMD's open-source stack (AMDGPU + Mesa + RADV) is the most viable path for
hardware GPU acceleration in UNHOX without proprietary driver dependencies.

---

## 12. DirectX Family (Microsoft)

| API | Year | Level |
|-----|------|-------|
| DirectDraw | 1995 | 2D blitting, surfaces |
| Direct3D 7 | 1999 | Fixed-function pipeline |
| Direct3D 9 | 2002 | Programmable shaders (SM 1.0–3.0) |
| Direct3D 10 | 2006 | Geometry shaders, unified shader model |
| Direct3D 11 | 2009 | Compute shaders (CS 5.0), tessellation |
| Direct3D 12 | 2015 | Explicit, low-overhead (like Vulkan) |
| DXGI | 2006 | Surface sharing, swap chains, display management |
| DirectML | 2018 | ML inference on D3D12 |
| DirectSR | 2024 | Super-resolution (DLSS/FSR abstraction) |

### D3D12 and Vulkan Equivalence

D3D12 and Vulkan are structurally equivalent explicit GPU APIs. SPIR-V (Vulkan)
and DXIL (D3D12) are both intermediate shader representations compiled to
hardware ISA by the driver.

**DXGI Shared Resources:** D3D12 supports `DXGI_SHARED_RESOURCE` — sharing GPU
surfaces between processes via NT HANDLE. This is the Windows equivalent of
dma-buf. UNHOX's Mach OOL memory + GPU device handle achieves the same result.

---

## 13. Alternative and Research Architectures

### Plan 9 `/dev/draw`

Plan 9's display interface is a kernel device file. Drawing commands are `write()`
calls to `/dev/draw`; the kernel executes them in the display driver. The window
system (rio) is a user-space process that multiplexes `/dev/draw`.

**Notable:** Plan 9's graphics are part of the kernel's device namespace —
simple, uniform, but not GPU-accelerated.

### Fresco / Berlin (ISO Display Model)

Research display server from the 1990s using CORBA IPC and a structured scene
graph. Ahead of its time; abandoned due to CORBA performance.

### Chromium (not the browser)

A cluster rendering system from Stanford/SGI that distributed OpenGL rendering
across a network of machines. The "sort-first" rendering model split the screen
into tiles handled by separate render nodes.

### ChromeOS Freon / Ozone

ChromeOS replaced X11 with Ozone — a platform abstraction layer for display and
input. On ChromeOS, display is handled by the Chrome browser process itself using
DRM/KMS directly; there is no separate display server process.

### Mir (Canonical)

Ubuntu's Canonical developed Mir as an alternative to Wayland for Ubuntu phones
(2013). Mir uses a custom IPC protocol, not the Wayland wire protocol, but
supports a Wayland compatibility layer. Used in IoT/Ubuntu Core.

### Fuchsia Scenic / Flatland

Google's Fuchsia OS uses Zircon (a microkernel) with Scenic as its display
compositor. Scenic uses a *scene graph* model (Flatland — 2D transform tree).
IPC is via Zircon FIDL (Fuchsia Interface Definition Language) over Zircon
channels (similar to Mach ports).

**Highly relevant to UNHOX:** Fuchsia's architecture (Zircon microkernel + Scenic
scene graph over FIDL) is structurally very close to what UNHOX will build.
Key differences: Zircon uses Zircon channels; UNHOX uses Mach ports.

### ScreenSpace / Compositor Protocols in Research OSes

- **seL4 + Wayland** — AGL (Automotive Grade Linux) and SECO have demonstrated
  Wayland compositing on seL4, with driver processes and the compositor running
  as separate seL4 tasks communicating via shared memory + notification objects.
- **Genode OS** — uses a scene graph compositor (Nitpicker) with Genode's
  capability-based IPC. Nitpicker is a 2D compositing server that clients talk
  to via Genode RPC.

---

## 14. Comparative Summary

| Architecture | IPC Mechanism | Compositor Role | GPU API | Security Model |
|---|---|---|---|---|
| X11/X.Org | X11 protocol (Unix socket) | Optional (Xcomposite) | OpenGL/GLX | Shared domain |
| Wayland | Wayland protocol (Unix socket) | Mandatory (compositor = server) | OpenGL/EGL/Vulkan | Per-surface isolation |
| NeXTSTEP DPS | Mach IPC | Integrated (WindowServer) | Display PostScript | Per-window rights |
| macOS Quartz | Mach IPC (private) | DWM (WindowServer) | Metal | Sandbox per app |
| Android | Binder IPC | SurfaceFlinger | OpenGL ES / Vulkan | SELinux per process |
| Windows | DXGI / ALPC | DWM | D3D12 | DACL per surface |
| Fuchsia/Scenic | Zircon FIDL | Scenic (scene graph) | Vulkan | Capability per object |
| Genode/Nitpicker | Genode RPC | Nitpicker | Software / Gallium | Capability per client |
| UNHOX (planned) | Mach IPC | Display Server (DPS-inspired) | Vulkan (Phase 5) | Port rights per window |

### Key Design Principles Distilled

1. **Compositor must own the scanout.** Post-X11 systems all follow this.
2. **Zero-copy buffer sharing.** dma-buf (Linux), DXGI shared resources (Windows),
   Mach OOL memory (UNHOX) — avoid CPU copies between app and compositor.
3. **Per-client isolation.** Capability / rights model, not shared domain.
4. **Explicit synchronisation.** GPU timeline fences, not implicit GL synchronisation.
5. **Scene graph over raw rasterisation.** Core Animation, Scenic, Flatland — the
   compositor works with a structured description of the scene, not raw pixels,
   enabling efficient damage tracking and hardware overlay use.

---

## 15. References

- Scheifler & Gettys, "The X Window System" (1986) — ACM TOCS 5(2)
- Høgsberg, "The Wayland Display Server" (2008) — Linux.conf.au
- Adobe Systems, "Display PostScript System Reference" (1990)
- Apple, "Core Animation Programming Guide" (2006–present)
- Kilgard, "OpenGL and Window System Integration" (1994)
- Khronos Group, "Vulkan 1.3 Specification" (2022)
- NVIDIA, "CUDA C++ Programming Guide" (2023)
- AMD, "ROCm Documentation" (2023) — https://rocm.docs.amd.com/
- Microsoft, "Direct3D 12 Programming Guide" (2023)
- Google, "Fuchsia Scenic / Flatland" — fuchsia.dev
- Genode, "Nitpicker GUI Server" — genode.org/documentation
- seL4 + Wayland: "Compositing on seL4" — SECO/AGL presentations
- NeXTSTEP DPS documentation → `archive/next-docs/`
- Wayland protocol spec → https://gitlab.freedesktop.org/wayland/wayland

See also:
- `docs/graphics-pipeline-microkernel.md` — GPU pipeline and AI/ML inference
  in the context of UNHOX's microkernel architecture
- `docs/rfcs/RFC-0002-display-server-architecture.md` — UNHOX display server RFC
