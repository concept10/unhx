# servers/display/x11/

X11 compatibility personality server.

Listens for X11 wire-protocol connections (on a UNHOX socket or pipe) and
translates them to UNHOX native display server Mach IPC calls.

## Status

Not started. Requires Phase B (`servers/display/core/`) to be complete.

## Architecture

```
X11 client (Xlib / XCB)
    │  Unix socket / pipe
    ▼
x11-personality (this server)
    │  Mach IPC (send right to unhx-display)
    ▼
unhx-display (core display server)
```

## References

- X11 protocol specification (x.org)
- XWayland source — reference implementation of X11-on-modern-compositor
- `docs/display-server-survey.md` §X11
