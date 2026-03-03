# frameworks/AppKit-backend/

UNHOX-native AppKit backend — connects GNUstep AppKit (libs-gui) to the UNHOX Display Server.

## Overview

AppKit provides the UNHOX application framework layer:
- **NSWindow / NSView** — application window and view hierarchy
- **NSEvent** — user input event delivery
- **NSApplication** — application run loop

## UNHOX Backend

The `appkit_backend.c` / `appkit_backend.h` files implement the
`GSDisplayServer` protocol against the UNHOX Display Server (DPS).
Instead of connecting to an X11 display or Wayland socket, the
backend sends Mach IPC messages directly to the display server's
port (`com.unhox.display`).

```
NSWindow → appkit_backend → Mach IPC → Display Server → VGA/Framebuffer
```

## Building

Phase 5 v1: the backend compiles as part of the kernel image. When
the UNHOX userspace ABI is established the backend will be a loadable
framework in `/Library/Frameworks/AppKit.framework/`.

## Status

- [x] AppKit backend header and implementation (`appkit_backend.h`, `appkit_backend.c`)
- [x] Window create/destroy/move/resize operations via IPC
- [x] Draw rect and text primitives via IPC
- [ ] GNUstep libs-gui submodule build configuration
- [ ] NSApplication run loop integration
- [ ] NSEvent delivery from keyboard driver

## Note on `frameworks/AppKit`

The `frameworks/AppKit` directory contains the `gnustep/libs-gui` submodule
(the full GNUstep AppKit library).  This backend lives separately in
`frameworks/AppKit-backend/` because it is UNHOX-original code, not a
modification to the upstream libs-gui source.

## References

- GNUstep libs-gui: https://github.com/gnustep/libs-gui
- GNUstep Back: https://github.com/gnustep/libs-back
- NeXTSTEP AppKit documentation (`archive/next-docs/`)
