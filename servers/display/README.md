# servers/display/

UNHOX Display Server — `unhx-display`.

A Mach-IPC-native compositing display server inspired by NeXTSTEP Display
PostScript (DPS) and macOS Quartz, incorporating security lessons from Wayland.

## Status

**Blocked** — awaiting L4 IPC improvements listed in
`docs/rfcs/RFC-0002-display-server-alternatives.md`:

- [ ] OOL memory descriptor support in IPC (pixel buffer transfer)
- [ ] Port right transfer in messages (surface/event port handoff)
- [ ] Blocking receive with timeout (event delivery)
- [ ] Bootstrap server port lookup (client discovery)

## Subdirectories

| Directory   | Contents |
|-------------|----------|
| `core/`     | `unhx-display` — primary Mach-IPC-native display server (Phase B) |
| `x11/`      | X11 personality server — translates X11 wire protocol to UNHOX native (Phase C) |
| `wayland/`  | Wayland personality server — translates Wayland protocol to UNHOX native (Phase C) |

## Architecture

```
X11 clients ──→ x11/ server ──┐
                               │   Mach IPC   ┌──→ Device Server (framebuffer)
Wayland clients → wayland/ ───┼─────────────→ core/ (unhx-display)
                               │              └──→ Device Server (input)
AppKit clients ───────────────┘
```

## IPC Protocol Summary

All clients communicate with `unhx-display` via Mach port send rights obtained
from the bootstrap server. Surfaces are shared via OOL memory descriptors.
Events are delivered to a per-client event port passed during connection setup.

Message IDs start at `DISPLAY_MSG_BASE = 4000`. See
`docs/rfcs/RFC-0002-display-server-alternatives.md` for the full protocol table.

## References

- `docs/display-server-survey.md` — comparative survey of all major display server architectures
- `docs/rfcs/RFC-0002-display-server-alternatives.md` — design RFC and gate conditions
- `frameworks/DisplayServer/README.md` — DisplayServer framework layer