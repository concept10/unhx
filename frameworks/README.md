# frameworks/

NeXT/OpenStep framework layer — synthesized from available open-source implementations.

## Subdirectories

| Directory        | Contents | Source |
|-----------------|----------|--------|
| `objc-runtime/` | Objective-C runtime (clang-compatible) | `gnustep/libobjc2` submodule |
| `Foundation/`   | Foundation Kit — NSObject, collections, run loops | `gnustep/libs-base` submodule |
| `AppKit/`       | Application Kit — UI framework | `gnustep/libs-gui` submodule |
| `DisplayServer/`| DPS-inspired compositor (new code) | UNHOX original |

## Adding Submodules

To initialize the GNUstep submodules after cloning:

```sh
git submodule update --init --recursive
```

## Phase 4 Deliverable

A GNUstep Foundation application running natively on UNHOX userspace.

## Phase 5 Deliverable

A full NeXT-heritage desktop with Workspace Manager and AppKit applications.
