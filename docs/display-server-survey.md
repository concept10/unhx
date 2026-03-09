# Display Server Architecture Survey

A comparative survey of display server architectures from macOS/NeXTSTEP,
X Window System, Wayland, Microsoft Windows, and BSD systems — with
analysis of each model's fit for the UNHOX microkernel.

---

## Table of Contents

1. [Why Display Servers Matter for UNHOX](#why-display-servers-matter-for-unhox)
2. [macOS / NeXTSTEP Lineage (Display PostScript → Quartz → Metal)](#macos--nextstep-lineage)
3. [X Window System (X11 / X.Org)](#x-window-system-x11--xorg)
4. [Wayland](#wayland)
5. [Microsoft Windows Display Architecture (GDI → DWM → WDDM)](#microsoft-windows-display-architecture)
6. [BSD Display Servers](#bsd-display-servers)
7. [Comparative Summary](#comparative-summary)
8. [Fit Analysis for UNHOX](#fit-analysis-for-unhox)

---

## Why Display Servers Matter for UNHOX

UNHOX is a Mach microkernel. The kernel provides only IPC, VM, tasks, and
threads. Everything else — including graphics — lives in userspace servers.

The Display Server question therefore reduces to: *what IPC protocol, privilege
model, and rendering pipeline should a UNHOX Display Server expose to
application clients?*

This document surveys the major families to inform that choice. The end goal
is to design a display server that can be tested as a pluggable userspace
personality on top of the core microkernel IPC layer, contingent on L4 IPC
improvements being landed first (see `docs/rfcs/RFC-0002-display-server-alternatives.md`).

---

## macOS / NeXTSTEP Lineage

### 1989–1997: NeXTSTEP — Display PostScript (DPS)

NeXTSTEP was the first commercial OS to ship a PostScript-based display server.
The architecture was directly shaped by the Mach microkernel underneath:

```
┌─────────────────────────────────────────────────────────┐
│  Application (user process)                              │
│  NXApp / AppKit sends PS display commands                │
├─────────────────────────────────────────────────────────┤
│  DPS Client Library  (ps_lib → pswrap wrappers)          │
│  wraps PS commands into Mach IPC messages                │
├─────────────────────────────────────────────────────────┤
│         Mach IPC (ports)  ← kernel boundary             │
├─────────────────────────────────────────────────────────┤
│  WindowServer (privileged userspace server)              │
│  • Interprets PostScript display commands                │
│  • Manages window list, clipping regions                 │
│  • Routes events to correct client port                  │
├─────────────────────────────────────────────────────────┤
│  NeXT hardware / framebuffer                             │
└─────────────────────────────────────────────────────────┘
```

Key properties of the DPS model:
- **IPC transport**: Mach ports (send drawing commands to WindowServer port).
- **Rendering model**: PostScript interpreter inside the server; apps describe
  *what* to draw (declarative), not *how* pixels should be manipulated.
- **Security**: Clients hold a Mach send right to the server port. The kernel
  enforces that only authorised right-holders can send.
- **Shared memory**: Large image data moved via Mach out-of-line memory
  descriptors rather than copying inline.

### 1995–1999: OPENSTEP / Rhapsody

OPENSTEP (the OpenStep standard on NeXT hardware + Windows NT) preserved the
DPS model. The Rhapsody project (Apple + NeXT merger) ran DPS on Mac hardware
during the transition period.

### 2001: macOS 10.0 Cheetah — Quartz (PDF-based)

Apple replaced DPS with **Quartz**, shifting from PostScript to PDF as the
imaging model. The WindowServer process remained a privileged server, but now
used Core Graphics (Quartz) drawing primitives. Communication with clients
still traversed Mach IPC.

```
┌─────────────────────────────────────────────────────────┐
│  Application (Cocoa / AppKit)                            │
├─────────────────────────────────────────────────────────┤
│  Core Graphics (CG) client library                       │
│  CGContext → CGS (Core Graphics Services) private API    │
├─────────────────────────────────────────────────────────┤
│  Mach IPC  (CGSConnectionID — Mach send right)           │
├─────────────────────────────────────────────────────────┤
│  WindowServer  (com.apple.windowserver)                  │
│  • Manages surface list (SkyLight layer)                 │
│  • Composites window surfaces (Quartz Compositor)        │
│  • Event routing (IOHIDEvent → CGEvent → app port)       │
├─────────────────────────────────────────────────────────┤
│  GPU / framebuffer (IOKit device server)                 │
└─────────────────────────────────────────────────────────┘
```

The private `CGS` (Core Graphics Services) API used Mach messages to cross
the client/server boundary. Apps hold a **CGSConnectionID**, which is ultimately
backed by a Mach send right to the WindowServer.

### 2002: macOS 10.2 Jaguar — Quartz Extreme

Quartz Extreme moved compositing from CPU to GPU, using OpenGL to blend window
surfaces on the graphics card. The WindowServer received each window's rendered
surface as a texture and composited them in hardware.

### 2007–2013: macOS 10.5–10.8 — Core Animation, Layer-Backed Views

Core Animation (introduced in Leopard) changed the client model: apps now
described their view hierarchy as a layer tree. The WindowServer received
committed layer trees and composited via OpenGL/Quartz 2D Extreme.

### 2014–2019: macOS 10.10–10.14 — Metal Integration

From macOS 10.10 Yosemite onward, WindowServer began transitioning its GPU
path from OpenGL to Metal. The Mach IPC client protocol remained stable, but
the server-side compositor moved to Metal shaders.

### 2020–2024: macOS 11–14 (Big Sur → Sonoma) — Metal-native

Metal became the exclusive GPU API inside WindowServer. IOSurface handles
(shared between client and server via Mach port) replaced older OpenGL texture
sharing. The SkyLight framework layer mediated the connection.

The protocol summary:
- Client → WindowServer: Mach messages (CGS protocol, ~1000 message IDs)
- Surface sharing: IOSurface + Mach port right
- Event delivery: Mach messages from WindowServer → application event port
- Rendering: Metal on GPU, CPU fallback via Core Graphics

### 2025–2026: macOS 15 Sequoia / macOS 26 — Projected Architecture

macOS 26 (the 2026 annual release; Apple adopted a year-based naming scheme
starting with macOS 26 in 2025) continues the Metal-native path. Key changes
that apply or are expected:

- **SkyLight private framework** remains the userspace-facing layer; no
  public ABI change.
- **WindowServer process** remains a launchd-managed privileged daemon (not in
  kernel); consistent with Mach microkernel heritage.
- **Metal 3 / Apple Silicon GPU** — all compositing is GPU-native on Apple
  Silicon; x86-64 (Intel) support dropped.
- **Display Server IPC** is still Mach messages over private CGS ports, now
  with tighter entitlement enforcement (only SkyLight clients can connect).
- **Spatial / Vision Pro display model**: macOS 26 borrows display geometry
  abstractions from visionOS for Projected Display and Stage Manager.

**Software stack (macOS 26, simplified):**

```
┌──────────────────────────────────────────────────────────────┐
│  App (SwiftUI / AppKit)                                       │
├──────────────────────────────────────────────────────────────┤
│  SkyLight.framework  (private; wraps CGS Mach IPC)           │
│  Core Animation  →  Metal rendering commands                 │
├──────────────────────────────────────────────────────────────┤
│  XPC / Mach IPC ──────────────────────────────────────────── │
├──────────────────────────────────────────────────────────────┤
│  WindowServer  (com.apple.windowserver)                      │
│  • SkyLight compositor (Metal shaders)                       │
│  • Window list, z-order, spaces                              │
│  • Displays (CoreDisplay, IOKit Display)                     │
│  • Event dispatch (IOHIDEvent → HIDEventSystem → app port)   │
├──────────────────────────────────────────────────────────────┤
│  Metal  →  GPU Command Queues  →  Display Engine (DCP)       │
│  (Display Controller Processor — separate CPU on Apple SoC)  │
├──────────────────────────────────────────────────────────────┤
│  XNU Kernel (Mach + BSD hybrid)                              │
│  IOKit  (GPU driver, IOSurface, IOHIDFamily)                 │
└──────────────────────────────────────────────────────────────┘
```

**Key macOS invariant across all versions**: WindowServer is a userspace
privileged process, not kernel code. This is architecturally identical to
what UNHOX wants to do.

---

## X Window System (X11 / X.Org)

X11 was designed in 1984 at MIT. It is the display server still used on most
Linux desktops today (either directly or via XWayland compatibility layer).

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│  X Client (application)                                  │
├─────────────────────────────────────────────────────────┤
│  Xlib / XCB  (client-side library)                       │
│  Formats requests per X11 wire protocol                  │
├─────────────────────────────────────────────────────────┤
│  Unix socket / TCP socket  (network transparency)        │
├─────────────────────────────────────────────────────────┤
│  X Server (X.Org / Xvfb / Xnest / XWayland)             │
│  • Manages display connections                           │
│  • Handles input events                                  │
│  • Draws on behalf of clients (rendering in server)      │
│  • Window Manager protocol (ICCCM / EWMH)                │
├─────────────────────────────────────────────────────────┤
│  DDX (Device-Dependent X) — kernel DRM/KMS driver        │
│  DRI (Direct Rendering Infrastructure)                   │
└─────────────────────────────────────────────────────────┘
```

### X11 Protocol

- Binary protocol over a socket (local Unix socket or TCP 6000+DISPLAY).
- Requests, replies, events, and errors; asynchronous by default.
- **Rendering**: historically the server drew on behalf of clients (X drawing
  primitives). Modern X11 clients use direct rendering (DRI/DRI2/DRI3) and
  send the rendered buffer to the server for compositing.
- **Extensions**: SHAPE, RENDER, COMPOSITE, DAMAGE, RANDR, etc. The protocol
  is extensible without breaking older clients.

### Security Model

- **Display access control**: `xhost` (coarse) or MIT-MAGIC-COOKIE (token
  per connection). No capability model — any client that can connect can read
  keystrokes from other clients (significant security weakness).
- **No isolation**: all clients share a single server namespace; one misbehaving
  client can snoop events from all others.

### Compositing

- The **Composite** extension (2004) added off-screen rendering; each window
  has a backing pixmap. A compositing manager (e.g., Compiz, Mutter, KWin)
  reads the pixmaps and assembles the final framebuffer.
- KMS/DRM: final scanout to monitor via kernel Direct Rendering Manager.

### Strengths and Weaknesses for UNHOX

| Aspect | X11 |
|--------|-----|
| Network transparency | ✅ (built in) |
| IPC mechanism | Socket (not Mach ports) |
| Security isolation | ❌ (poor; shared namespace) |
| Compositing model | Add-on extension (not first-class) |
| Rendering location | Server-side (legacy) or client-side (DRI) |
| Protocol complexity | Very high (decades of extensions) |
| Microkernel fit | Poor (assumes Unix socket, not Mach IPC) |

---

## Wayland

Wayland is a modern display protocol created in 2008 by Kristian Høgsberg,
designed from scratch to fix X11's compositing and security problems.

### Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Wayland Client (application)                                │
├──────────────────────────────────────────────────────────────┤
│  libwayland-client  (object-oriented protocol)               │
│  + toolkit (GTK4, Qt6, SDL2, EFL, ...)                       │
├──────────────────────────────────────────────────────────────┤
│  Unix socket  (wayland-0 or WAYLAND_DISPLAY env)             │
├──────────────────────────────────────────────────────────────┤
│  Wayland Compositor  (wl_compositor interface)               │
│  Examples: Weston, Mutter (GNOME), KWin (KDE), sway,        │
│            cosmic-comp, River, Wayfire                       │
│  • Compositor IS the display server (merged role)            │
│  • Manages surface list and compositing                      │
│  • Input routing per-surface (secure)                        │
│  • Client buffers via wl_buffer (dmabuf or shm)              │
├──────────────────────────────────────────────────────────────┤
│  KMS/DRM  (kernel direct rendering — Linux-specific)         │
└──────────────────────────────────────────────────────────────┘
```

### Protocol Design

Wayland uses an **object-oriented wire protocol** over a Unix socket:
- Objects (compositor, surfaces, seats, outputs) are referenced by ID.
- Requests: client → compositor.
- Events: compositor → client.
- **Interfaces** are defined in XML (`.xml` protocol files); `wayland-scanner`
  generates C stubs.

Wayland **merged** the roles: the compositor *is* the display server *and* the
window manager. This eliminates the ICCCM/EWMH complexity of X11.

### Buffer Sharing

Clients render into **wl_buffer** objects:
- **wl_shm**: shared memory (anonymous mmap).
- **linux-dmabuf** (extension): dma-buf file descriptor for zero-copy GPU
  buffer sharing.

The compositor never copies pixel data; it imports the buffer by file
descriptor and reads it directly for compositing.

### Security Model

- Each client has its own connection and private object namespace.
- Input events are only sent to the focused surface (no eavesdropping).
- Screen capture requires explicit protocol (portal or wlr-screencopy).
- No equivalent to xhost: access is per-connection, enforced by the compositor.

### Strengths and Weaknesses for UNHOX

| Aspect | Wayland |
|--------|---------|
| Security isolation | ✅ (per-surface input, private namespaces) |
| IPC mechanism | Unix socket (adaptable) |
| Buffer sharing | ✅ (dmabuf / shm) |
| Compositing model | ✅ (first-class; compositor = display server) |
| Network transparency | ❌ (not built in; requires XWayland for remote) |
| Protocol complexity | Moderate (extensible via XDG/wp-* protocols) |
| Microkernel fit | Good model, but socket-based (needs Mach port adaptation) |

The Wayland **security model** and **compositing-as-first-class** design are
the strongest architectural lessons for UNHOX.

---

## Microsoft Windows Display Architecture

### Pre-Vista: GDI and the Win32k.sys Window Server

Before Windows Vista, the window manager and GDI graphics subsystem ran
**inside the kernel** (`win32k.sys`):

```
┌──────────────────────────────────────────────────────────────┐
│  Win32 Application                                           │
├──────────────────────────────────────────────────────────────┤
│  User32.dll / GDI32.dll  (system call wrappers)              │
├──────────────────────────────────────────────────────────────┤
│  kernel-mode Win32k.sys                                      │
│  • USER subsystem (windows, messages, input)                 │
│  • GDI subsystem (drawing primitives, fonts)                 │
│  • Direct access to framebuffer                              │
└──────────────────────────────────────────────────────────────┘
```

This was a pragmatic performance decision (fewer user/kernel transitions) but
violated the principle of least privilege. `win32k.sys` became one of the
largest attack surfaces in Windows.

### Vista / Windows 7: WDDM + Desktop Window Manager (DWM)

Windows Vista introduced the **Windows Display Driver Model (WDDM)** and the
**Desktop Window Manager (DWM)**:

```
┌──────────────────────────────────────────────────────────────┐
│  Win32 Application                                           │
├──────────────────────────────────────────────────────────────┤
│  User32.dll / DWM client API                                 │
├──────────────────────────────────────────────────────────────┤
│  Desktop Window Manager (DWM.exe)  ← userspace process      │
│  • Composites all application windows via Direct3D           │
│  • Manages Aero Glass effects, thumbnails                    │
│  • Each app window = off-screen Direct3D surface             │
├──────────────────────────────────────────────────────────────┤
│  WDDM kernel-mode driver                                     │
│  • Virtualised GPU access per-process (scheduler)            │
│  • Memory management for GPU allocations                     │
├──────────────────────────────────────────────────────────────┤
│  GPU hardware                                                │
└──────────────────────────────────────────────────────────────┘
```

DWM.exe is analogous to a Wayland compositor or macOS WindowServer: a
privileged userspace process that composites application surfaces via the GPU.

### Windows 8–11 / Windows 12: Evolution of DWM

- **Windows 8**: DWM always on (Aero disabled in theme but compositing
  always active). Win32k.sys still exists but win32k isolation (win32kfull /
  win32kbase split) began.
- **Windows 10/11**: DirectComposition API for low-latency, batched surface
  updates. WDDM 2.x adds hardware scheduling (GPU can schedule its own work).
- **Win32k mitigation**: Microsoft AppContainer sandbox can block win32k
  syscalls for UWP/sandboxed apps — progressive move away from kernel GDI.

### Windows Display Server Summary

| Aspect | Windows (WDDM + DWM) |
|--------|----------------------|
| IPC mechanism | LPC (Local Procedure Call) / ALPC |
| Security isolation | DWM runs as SYSTEM; per-process GPU memory isolation |
| Compositing | ✅ (DWM, DirectComposition, Direct3D) |
| Kernel coupling | High legacy (win32k.sys still in kernel) |
| Microkernel fit | DWM model is good; win32k coupling is the anti-pattern |

The key lesson from Windows: **moving win32k from kernel to userspace improved
security dramatically**. UNHOX starts with the correct invariant by design.

---

## BSD Display Servers

BSD-family operating systems have historically not shipped their own display
server; they inherit X11 and, more recently, Wayland.

### FreeBSD

- **Primary display server**: X.Org (via FreeBSD ports or pkg).
- **Wayland**: Growing support; `wayland`, `weston`, `sway` all work.
- **drm-kmod**: FreeBSD port of Linux DRM/KMS for GPU drivers (Intel, AMD,
  Radeon). Required for hardware-accelerated Wayland.
- **Linuxulator**: Linux binary compat layer allows running Linux Electron apps
  (which may use Wayland natively).

### NetBSD

- X11 via `pkgsrc`.
- No Wayland support as of writing (DRM/KMS infrastructure incomplete).
- Framebuffer console (rasops) used for early boot display.

### OpenBSD

- **xenocara**: OpenBSD's fork/integration of X.Org, tightly maintained.
- Security-hardened: X.Org runs as unprivileged user (`_x11`), with privilege
  separated components.
- No Wayland (OpenBSD developers have expressed preference for X11 model with
  privilege separation over the merged Wayland compositor model).
- **wscons** console: kernel framebuffer for text console.

### Haiku OS (BSD-inspired userland)

Haiku (BeOS successor) ships its own original display server — the closest
analogy to what UNHOX aims to build:

```
┌──────────────────────────────────────────────────────────────┐
│  Haiku Application (BApplication)                            │
├──────────────────────────────────────────────────────────────┤
│  libbe.so  (BeAPI client library)                            │
├──────────────────────────────────────────────────────────────┤
│  BMessage IPC  (Haiku IPC, not Mach)                         │
├──────────────────────────────────────────────────────────────┤
│  app_server  (privileged userspace server)                   │
│  • Manages window list, decorators                           │
│  • Renders via BView drawing primitives                      │
│  • Input routing (cursor, keyboard)                          │
├──────────────────────────────────────────────────────────────┤
│  Accelerant API  (userspace GPU driver plugin)               │
└──────────────────────────────────────────────────────────────┘
```

Haiku's `app_server` is architecturally the closest to what UNHOX needs:
- A **privileged userspace display server** receiving draw commands via IPC.
- Clean client/server split with a well-defined protocol.
- But IPC is BMessage over a port (similar in concept to Mach ports, but
  different wire format).

### DragonFly BSD

- X11 via ports.
- No native display server.

### BSD Summary

| System | Display Server | IPC | Notes |
|--------|---------------|-----|-------|
| FreeBSD | X.Org / Wayland | Unix socket | DRM/KMS via drm-kmod |
| NetBSD | X.Org | Unix socket | No Wayland |
| OpenBSD | xenocara (X.Org fork) | Unix socket | Security-hardened |
| Haiku | app_server (original) | BMessage | Most relevant to UNHOX |

---

## Comparative Summary

```
┌────────────────────┬──────────────┬───────────────┬──────────────────┬───────────────┐
│ System             │ IPC          │ Client Buffer │ Compositing      │ Security      │
├────────────────────┼──────────────┼───────────────┼──────────────────┼───────────────┤
│ macOS/NeXTSTEP DPS │ Mach ports   │ OOL memory    │ Server-side (PS) │ Port rights   │
│ macOS Quartz/Metal │ Mach/XPC     │ IOSurface     │ Metal GPU        │ Entitlements  │
│ X11 (X.Org)        │ Unix socket  │ Pixmap/DRI3   │ Compositing ext  │ Weak (xhost)  │
│ Wayland            │ Unix socket  │ dmabuf/shm    │ Compositor first │ Per-surface   │
│ Windows DWM        │ ALPC         │ D3D surface   │ DirectComposition│ Process-level │
│ Haiku app_server   │ BMessage     │ BBitmap       │ Server-side      │ Port-based    │
└────────────────────┴──────────────┴───────────────┴──────────────────┴───────────────┘
```

### Key Takeaways

1. **All modern display servers run in userspace** — the kernel provides only
   memory sharing and IPC primitives. UNHOX is correct to put the display
   server outside the kernel.

2. **IPC is the transport; not the rendering model** — macOS uses Mach ports,
   Wayland uses Unix sockets, Windows uses ALPC, Haiku uses BMessage ports.
   The *protocol* can be adapted to any IPC mechanism. For UNHOX, Mach ports
   are the natural choice.

3. **Buffer sharing is critical for performance** — all modern servers use
   zero-copy buffer sharing (IOSurface/dmabuf/D3D resource). Mach OOL memory
   (`MACH_MSG_OOL_DESCRIPTOR`) provides the UNHOX equivalent.

4. **Security comes from IPC capability model** — Wayland's best-in-class
   security comes from private connections and per-surface input routing.
   Mach port rights provide an even stronger capability model. UNHOX can
   exceed Wayland security with proper port right design.

5. **Compositing as first-class** — Wayland's lesson: make compositing the
   primary model, not an add-on (as X11's COMPOSITE extension was). The
   UNHOX display server should composite by default.

---

## Fit Analysis for UNHOX

### Recommended Approach: Mach-IPC-Native Display Server (DPS-Inspired)

Given UNHOX's microkernel heritage and design goals, the recommended approach
is a **Mach-IPC-native display server** drawing on lessons from all of the
above systems:

| Design Choice | Rationale |
|---------------|-----------|
| **Transport**: Mach port IPC | Native to UNHOX; capability-based security; zero-copy OOL |
| **Security model**: Port rights | Clients hold a send right to the display server port; the kernel enforces access |
| **Buffer sharing**: Mach OOL descriptors | Equivalent to IOSurface / dmabuf; no copy of pixel data |
| **Rendering model**: Compositing-first | Wayland lesson; each client renders into its own surface |
| **Protocol style**: NeXTSTEP DPS heritage | Familiar to the NeXT/macOS ecosystem; declarative drawing model |
| **Window management**: Separate server | Clean microkernel split; display server ≠ window manager |

### Alternative Personalities (Post-L4 IPC)

After L4 IPC improvements are landed, each display server protocol family
can be run as a **compatibility personality server** translating its native
IPC to the UNHOX Mach-native display server:

| Personality Server | Translates | Status |
|-------------------|------------|--------|
| `display/dps/`    | NeXTSTEP DPS commands → UNHOX native | Primary target |
| `display/xwayland/` | X11 wire protocol → UNHOX native | Compatibility layer |
| `display/wayland/`  | Wayland protocol → UNHOX native | Compatibility layer |

This mirrors how XWayland translates X11 clients to a Wayland compositor,
and how macOS's XQuartz ran X11 on top of Quartz/WindowServer.

### Prerequisites

Before a display server can be started as an approved server:

1. ✅ L4 IPC baseline (`docs/rfcs/RFC-0001-ipc-message-format.md`)
2. ⬜ L4 IPC improvements (OOL descriptors, port right delegation)
3. ⬜ Device Server (framebuffer access via device server port)
4. ⬜ VFS Server (font files, cursor resources)
5. ⬜ GPU driver in Device Server (or initial software rasteriser)

See `docs/rfcs/RFC-0002-display-server-alternatives.md` for the formal proposal.

---

## References

### macOS / NeXTSTEP
- NeXTSTEP Release 3 Developer Documentation, "Display PostScript" (archive.org)
- Apple Tech Note TN2007 — Quartz Compositor internals
- Amit Singh, *Mac OS X Internals* (2006), Chapter 10: The Windowing System
- WWDC sessions on Core Animation and Metal (developer.apple.com)
- `xnu` source: `osfmk/mach/`, `iokit/` (github.com/apple-oss-distributions/xnu)

### X Window System
- Scheifler & Gettys, "The X Window System" (ACM TOCS, 1986)
- X.Org Foundation — X11 protocol specification (x.org)
- Nickle & Packard, "DRI3 and Present" (XDC 2014)

### Wayland
- Høgsberg, "The Wayland Display Server" (LCA 2009)
- Wayland protocol specification (wayland.freedesktop.org)
- Wayland Book — Daniel Stone & Simon Ser (wayland-book.com)
- wl-roots project — Wayland compositor construction toolkit

### Microsoft Windows
- Mark Russinovich et al., *Windows Internals* 7th ed., Chapter 12: Graphics and Desktop
- "Understanding the Windows Desktop Window Manager" — MSDN / docs.microsoft.com
- WDDM architecture — Microsoft Hardware Dev Center

### BSD Systems
- FreeBSD Handbook, Chapter 5: X Window System
- OpenBSD xenocara project (xenocara.org)
- Haiku OS `app_server` source (github.com/haiku/haiku/tree/master/src/servers/app)
- "The Design of the Haiku Operating System" — BeGeistert proceedings

### Mach / UNHOX
- `docs/ipc-design.md` — UNHOX IPC design decisions
- `docs/rfcs/RFC-0001-ipc-message-format.md` — UNHOX message format
- `docs/rfcs/RFC-0002-display-server-alternatives.md` — UNHOX display server RFC
- `frameworks/DisplayServer/README.md` — DisplayServer implementation plan