# user/workspace/

UNHOX Workspace Manager — the outermost application layer of the UNHOX desktop.

## Overview

The Workspace Manager mirrors the role of **GWorkspace** in GNUstep and the
original NeXTSTEP Workspace Manager.  It owns:

- The desktop background
- The menu bar (handled by the display server compositor in Phase 5 v1)
- Application windows: About panel, file browser, terminal

## Architecture

```
workspace_main()
    ├── appkit_backend_init()          ← connect to display server
    ├── appkit_window_create(...)      ← "About UNHOX" panel
    ├── appkit_window_create(...)      ← "Workspace" file browser
    ├── appkit_window_create(...)      ← "Terminal" window
    └── event loop (NSEvent pump)     ← Phase 5 v2
```

## Milestone v1.0

The Workspace Manager is responsible for the **v1.0 milestone**:

> **"NeXT-heritage desktop boots"**

Success is reported as:
```
[workspace] Milestone v1.0 PASS — NeXT-heritage desktop up
```
on the serial console.

## Future Work

- NSApplication run loop with NSEvent delivery
- GWorkspace file browser with ramfs integration
- Dock / application launcher
- Preferences panel
