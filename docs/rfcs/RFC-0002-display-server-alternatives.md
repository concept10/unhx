# RFC-0002: Display Server Alternatives for UNHOX

- **Status**: Draft
- **Author**: UNHOX Project
- **Date**: 2026-03-06
- **Depends-on**: RFC-0001 (IPC Message Format), IPC-IMP-1 through IPC-IMP-4 (OOL descriptors, port right transfer, blocking receive, bootstrap lookup — defined in §Required IPC Improvements)

## Summary

This RFC analyses the major display server architectures — macOS/NeXTSTEP
Display PostScript (DPS), Quartz/Metal, X11, Wayland, Windows DWM, and BSD
`app_server` models — and proposes a strategy for the UNHOX display server.
The display server will be implemented as an approved L4 Mach IPC userspace
server *after* the four IPC improvements described below (IPC-IMP-1 through
IPC-IMP-4) are implemented and passing their milestone tests.

For the full technology survey see `docs/display-server-survey.md`.

## Motivation

UNHOX Phase 5 (`docs/roadmap.md`) includes a display server. Before writing
any code it is necessary to:

1. Choose which rendering model to adopt as primary.
2. Understand which compatibility layers (X11, Wayland) will be required.
3. Identify the IPC improvements that must land before the display server
   can start (the "approved L4 IPC improvements" prerequisite).
4. Agree on a testing strategy that allows all approaches to be evaluated.

## Background: Display Server Landscape

The five main families are summarised here; the full comparison is in
`docs/display-server-survey.md`.

| System             | IPC Transport | Buffer Sharing | Security Model   |
|--------------------|---------------|----------------|------------------|
| NeXTSTEP DPS       | Mach ports    | OOL memory     | Port rights      |
| macOS Quartz/Metal | Mach / XPC    | IOSurface      | Entitlements     |
| X11 / X.Org        | Unix socket   | Pixmap / DRI3  | Weak (xhost)     |
| Wayland            | Unix socket   | dmabuf / shm   | Per-surface      |
| Windows DWM        | ALPC          | D3D surface    | Process level    |
| Haiku app_server   | BMessage port | BBitmap        | Port-based       |

The NeXTSTEP DPS model is the most natural fit for UNHOX:
- IPC transport is already Mach ports.
- Security is already enforced by port rights (no separate mechanism needed).
- Buffer sharing uses Mach OOL descriptors (the UNHOX equivalent of IOSurface).
- The NeXT/OPENSTEP heritage is part of the project identity.

## Required IPC Improvements (Gate Condition)

The display server cannot be started as an approved server until the following
IPC capabilities are implemented and passing their milestone tests:

### IPC-IMP-1: Out-of-Line Memory Descriptors

Pixel buffers cannot be copied inline through IPC messages — a 4K×2K BGRA
framebuffer is 32 MB. Mach OOL (`MACH_MSG_OOL_DESCRIPTOR`) transfers a VM
region by remapping pages rather than copying bytes.

**Required changes:**
- `kernel/ipc/ipc_kmsg.c` — handle `MACH_MSG_OOL_DESCRIPTOR` in send/receive
- `kernel/vm/vm_map.c` — implement copy-on-write page remapping for OOL
- Test: `tests/ipc/ipc_ool_test.c` — send a 1 MB buffer, verify received

### IPC-IMP-2: Port Right Transfer in Messages

The display server needs to hand a reply port to a new client and pass
surface handles (backed by port rights) to clients. Port descriptors in
complex messages (`MACH_MSG_PORT_DESCRIPTOR`) must be implemented.

**Required changes:**
- `kernel/ipc/ipc_kmsg.c` — handle `MACH_MSG_PORT_DESCRIPTOR`, translate
  port names across task spaces during copy-out
- `kernel/ipc/ipc_right.c` — implement MOVE_SEND, COPY_SEND, MOVE_SEND_ONCE
  semantics for message transfer
- Test: `tests/ipc/ipc_port_transfer_test.c` — server passes a port right
  to a client, client uses it to send messages

### IPC-IMP-3: Blocking Receive with Timeout

Window event delivery requires the client to block waiting for the display
server's event port. The `mach_msg()` trap must support:
- `MACH_RCV_TIMEOUT` — timed blocking on receive
- `MACH_MSG_TIMEOUT_NONE` — block indefinitely until a message arrives

**Required changes:**
- `kernel/ipc/ipc_mqueue.c` — sleeping wait with timeout
- `kernel/kern/thread.c` — thread sleep / wakeup linked to mqueue
- Test: `tests/ipc/ipc_timeout_test.c` — receive with 100 ms timeout, verify
  `MACH_RCV_TIMED_OUT` error

### IPC-IMP-4: Bootstrap Server Port Lookup

The display server needs to be discoverable. Clients call the bootstrap server
to look up the display server's well-known port.

**Required changes:**
- `servers/bootstrap/` — implement `bootstrap_look_up()` RPC
- Test: server registers, client looks up, client sends to server

When all four improvements are implemented and their tests pass, the display
server is unblocked.

## Proposed Design: UNHOX Display Server (unhx-display)

### Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Application  (AppKit / custom)                              │
├──────────────────────────────────────────────────────────────┤
│  libdisplay.a  (client library)                              │
│  • display_connect()  → Mach send right to server            │
│  • surface_create()   → allocates OOL buffer, sends to srv   │
│  • surface_commit()   → triggers compositing                 │
│  • event_receive()    → blocking receive on event port       │
├──────────────────────────────────────────────────────────────┤
│  Mach IPC  (port rights + OOL descriptors)                   │
├──────────────────────────────────────────────────────────────┤
│  unhx-display  (privileged userspace server)                 │
│  • Window list management (z-order, focus)                   │
│  • Composites client surfaces → master framebuffer           │
│  • Dispatches input events to focused window port            │
│  • Software rasteriser (Phase 1); GPU backend (Phase 2)      │
├──────────────────────────────────────────────────────────────┤
│  Device Server  (framebuffer, input devices)                 │
│  Mach IPC to device server port for framebuffer access       │
└──────────────────────────────────────────────────────────────┘
```

### IPC Protocol (Initial)

Message IDs are assigned starting at `DISPLAY_MSG_BASE = 4000`.

| ID   | Name                  | Direction      | Description                      |
|------|-----------------------|----------------|----------------------------------|
| 4001 | DISPLAY_CONNECT       | client → server | Register client; receive event port |
| 4002 | DISPLAY_DISCONNECT    | client → server | Unregister client                |
| 4010 | SURFACE_CREATE        | client → server | Allocate surface; returns surface ID |
| 4011 | SURFACE_COMMIT        | client → server | Mark surface ready for compositing |
| 4012 | SURFACE_DESTROY       | client → server | Release surface                  |
| 4020 | WINDOW_CREATE         | client → server | Attach surface to a window       |
| 4021 | WINDOW_SET_TITLE      | client → server | Set window title string          |
| 4022 | WINDOW_RESIZE         | client → server | Request window resize            |
| 4030 | EVENT_KEY             | server → client | Key press/release event          |
| 4031 | EVENT_POINTER         | server → client | Mouse/pointer move/click event   |
| 4032 | EVENT_EXPOSE          | server → client | Surface region needs redraw      |

Surfaces are transferred as OOL memory descriptors (`IPC-IMP-1`). The event
port is transferred via a port descriptor in the DISPLAY_CONNECT reply
(`IPC-IMP-2`).

### Compatibility Personality Servers

After the primary UNHOX display server is running, compatibility personalities
translate foreign protocols to the native Mach protocol:

```
X11 clients ──→ xserver-unhx ──→ Mach IPC ──→ unhx-display
Wayland clients → wayland-unhx → Mach IPC ──→ unhx-display
```

- `servers/display/x11/` — X11 wire protocol listener on a Unix-domain socket
  (or UNHOX socket server), translating X11 requests to UNHOX display messages.
- `servers/display/wayland/` — Wayland compositor interface on a
  `wayland-0` socket, translating wl_surface commits to UNHOX surface commits.

Each personality server is itself a Mach IPC task — it holds a send right to
the `unhx-display` server port, just like any other client.

### Testing Strategy

Each technology can be tested in isolation using QEMU:

1. **UNHOX native** (primary): AppKit app → libdisplay → unhx-display →
   framebuffer. Pass/fail: window appears and receives keyboard events.

2. **X11 personality**: `xterm` linked against standard Xlib →
   xserver-unhx → unhx-display. Pass/fail: xterm renders text.

3. **Wayland personality**: `weston-terminal` → wayland-unhx → unhx-display.
   Pass/fail: terminal renders text.

4. **Benchmark**: IPC round-trip latency from client surface commit to
   framebuffer scanout. Target: < 1 ms at 60 Hz.

## Implementation Phases

### Phase A — IPC gate conditions (blocks Phase B)

- [ ] Implement OOL memory descriptors (`IPC-IMP-1`)
- [ ] Implement port right transfer in messages (`IPC-IMP-2`)
- [ ] Implement blocking receive with timeout (`IPC-IMP-3`)
- [ ] Implement bootstrap server port lookup (`IPC-IMP-4`)
- [ ] All four pass their unit tests

### Phase B — Minimal display server (software rasteriser)

- [ ] Create `servers/display/` directory structure
- [ ] Implement `unhx-display` core loop (Mach message receive loop)
- [ ] Implement surface management (OOL buffer allocation, window list)
- [ ] Implement software compositor (CPU blit of surfaces to framebuffer)
- [ ] Framebuffer output via device server
- [ ] Implement `libdisplay.a` client library
- [ ] Write a minimal AppKit test application using libdisplay
- [ ] Verify milestone v0.8: a window appears on screen

### Phase C — Compatibility layers

- [ ] Implement X11 personality server (`servers/display/x11/`)
- [ ] Implement Wayland personality server (`servers/display/wayland/`)
- [ ] Test X11 client (xterm) via x11 personality
- [ ] Test Wayland client (weston-terminal) via wayland personality

### Phase D — GPU acceleration

- [ ] Device server exposes GPU command queue port
- [ ] unhx-display compositor moves to GPU-accelerated blit
- [ ] Metal-style command buffer protocol (future)

## Alternatives Considered

### Alternative 1: X11 as primary display server

Run X.Org server as the UNHOX display server. Rejected because:
- X11's IPC is socket-based, not Mach-port-based.
- Security model is weak (shared namespace, xhost).
- Compositing is an add-on, not first-class.
- Does not leverage UNHOX's capability model.

X11 will be supported as a *compatibility personality* (see Phase C), not
as the primary server.

### Alternative 2: Wayland compositor as primary

Run a Wayland compositor (e.g., Weston or sway) as the primary UNHOX display
server. Rejected because:
- Wayland is socket-based; requires wrapping in a socket server task.
- Wayland assumes Linux DRM/KMS for display scanout; UNHOX has a device
  server, not DRM/KMS.
- Loses the NeXT/Mach heritage that motivates the project.

Wayland's security model and compositing design are incorporated as
*lessons* into the native UNHOX design.

### Alternative 3: Haiku app_server port

Port Haiku's `app_server` to UNHOX. Rejected because:
- `app_server` uses BMessage IPC (not Mach ports); requires significant
  translation layer.
- Haiku has no GPU acceleration path compatible with UNHOX device server.

Haiku's clean server/client architecture is referenced as a design model.

## References

- `docs/display-server-survey.md` — full technology survey
- `docs/rfcs/RFC-0001-ipc-message-format.md` — UNHOX IPC message format
- `frameworks/DisplayServer/README.md` — DisplayServer framework stubs
- NeXTSTEP DPS documentation (`archive/next-docs/`)
- Wayland protocol specification (wayland.freedesktop.org)
- Haiku `app_server` source (github.com/haiku/haiku/tree/master/src/servers/app)
