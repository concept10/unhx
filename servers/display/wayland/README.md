# servers/display/wayland/

Wayland compatibility personality server.

Presents a Wayland compositor interface (wl_compositor, wl_surface, xdg_toplevel)
to Wayland clients and translates commits to UNHOX native display server
Mach IPC calls.

## Status

Not started. Requires Phase B (`servers/display/core/`) to be complete.

## Architecture

```
Wayland client (libwayland-client)
    │  Unix socket (WAYLAND_DISPLAY)
    ▼
wayland-personality (this server)
    │  Mach IPC (send right to unhx-display)
    ▼
unhx-display (core display server)
```

## Design Notes

- Buffer sharing: Wayland dmabuf extension maps to Mach OOL descriptors.
- Input: Wayland wl_seat events translated to UNHOX EVENT_KEY / EVENT_POINTER.
- No XDG portal support in Phase 1 — basic wl_surface and xdg_toplevel only.

## References

- Wayland protocol specification (wayland.freedesktop.org)
- Wayland Book — Daniel Stone & Simon Ser
- `docs/display-server-survey.md` §Wayland