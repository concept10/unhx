# NeXTSTEP / OPENSTEP Documentation Archive

This directory contains or should contain documentation for the NeXT software ecosystem - the primary design reference for UNHOX's framework layer (AppKit, Foundation, Display Server).

## What is NeXTSTEP?

**NeXTSTEP** is a UNIX object-oriented operating system from NeXT Computer Inc. (1989-1995):

### System Stack
```
Application Layer
  ↓
AppKit (Window Server, GUI)
  ↓
Foundation, EOF (frameworks)
  ↓
Objective-C Runtime
  ↓
Display PostScript (graphics)
  ↓
Mach Microkernel + BSD
  ↓
Hardware
```

### Historical Significance
- **First object-oriented OS** at scale
- **Objective-C native** (not an afterthought)
- **NeXT Cube** — revolutionary workstation (1988)
- **Precursor to macOS/iOS** — Apple acquired NeXT (2001)
- **Influenced** — Windows (NT display server), KDE/GNOME design

### Design Philosophy
- Everything is an object (OOP pervasive)
- Clean separation of concerns (layered architecture)
- Network transparency (DO = Distributed Objects)
- Elegant APIs designed for developer productivity

## Key Documentation to Archive

### Core System

1. **NeXTSTEP General Reference** (Release 3.3)
   - System overview and concepts
   - Architecture and design principles
   - Installation and configuration

2. **Mach OS (NeXTSTEP Kernel Technical Notes)**
   - Mach microkernel documentation
   - Task, port, message passing
   - Exception handling
   - Memory management

### Application Frameworks

3. **Object-Oriented Programming and the Objective-C Language**
   - Language specification
   - Runtime behavior
   - Memory management (reference counting)
   - Introspection capabilities

4. **NeXTSTEP Objective-C Runtime Reference**
   - Function prototypes (class, object, selector APIs)
   - Method invocation details
   - Forward declarations
   - Protocol support

### Graphics & Display

5. **Display PostScript Programming Guide**
   - PostScript graphics model
   - DPS client library API
   - Coordinate transformations
   - Device contexts

6. **AppKit Reference and Guide**
   - View/Window hierarchy
   - Responder chain (event handling)
   - Drawing and rendering
   - Menus, buttons, controls
   - InterfaceBuilder (NIB format)

### Foundation Framework

7. **Foundation Kit Reference**
   - NSObject, NSArray, NSDictionary, NSString
   - File I/O and paths
   - Property lists (.plist format)
   - Key-value coding (KVC)
   - Archiving/serialization

8. **OPENSTEP Specification** (Standards version)
   - API compatibility layer
   - Multi-platform (Mach, Solaris, Windows)
   - Reduced dependencies

## Where to Obtain NeXTSTEP Documentation

### Primary Archives

1. **Bitsavers** (comprehensive PDF collection)
   ```
   https://bitsavers.org/pdf/next/
   
   Includes:
   - Release notes
   - Developer guides  
   - API references
   - Technical notes
   ```

2. **Archive.org NeXT Collection**
   ```
   https://archive.org/search.php?query=NeXTSTEP
   https://archive.org/details/NeXTComputer
   ```

3. **GitHub Collections**
   ```bash
   search: nextstep-docs, openstep-documentation
   # Community members maintaining PDF mirrors
   ```

### Manual Download Process

```bash
# 1. Visit Bitsavers
curl -l https://bitsavers.org/pdf/next/ | grep -o 'href="[^"]*"'

# 2. Download specific PDFs
wget https://bitsavers.org/pdf/next/ApplicationKitReference.pdf
wget https://bitsavers.org/pdf/next/FoundationReference.pdf
wget https://bitsavers.org/pdf/next/ObjectiveCLanguage.pdf

# 3. Organize into:
archive/next-docs/
  ├── NeXTSTEP/
  │   ├── GeneralReference.pdf
  │   ├── SystemAdministration.pdf
  │   └── KernelReference.pdf
  ├── Objective-C/
  │   ├── ObjectiveCLanguage.pdf
  │   └── RuntimeReference.pdf
  ├── AppKit/
  │   ├── AppKitReference.pdf
  │   └── ProgrammingGuide.pdf
  ├── Foundation/
  │   ├── FoundationReference.pdf
  │   └── FoundationGuide.pdf
  ├── Graphics/
  │   ├── DisplayPostScript.pdf
  │   └── GraphicsGuide.pdf
  └── OPENSTEP/
      └── OPENSTEPSpecification.pdf
```

## Key Design Concepts (For UNHOX)

### 1. AppKit/Window Server Architecture
```
Display Server (PostScript-based)
  ↓
Window Manager (event distribution)
  ↓
View Hierarchy (responder chain)
  ↓
Application Event Loop
```

UNHOX `frameworks/DisplayServer/` studies this pattern:
- Clean window/view separation
- Introspectable responder chain
- Event routing (keyboard, mouse, custom)

### 2. Foundation Framework
```
NSObject (base class)
  ├── Collections: NSArray, NSDictionary, NSSet
  ├── Strings: NSString, NSData
  ├── I/O: NSFileManager, NSStream
  └── Encoding: NSCoder, NSPropertyListSerialization
```

UNHOX `frameworks/Foundation/` adapts:
- Reference counting semantics
- Collections API design
- Archiving/serialization patterns

### 3. Objective-C Runtime
```
Class/Instance hierarchy
Method dispatch (dynamic lookup)
Message passing (selector + arguments)
Protocol definitions
Memory management (retain/release)
```

UNHOX `frameworks/objc-runtime/` integrates:
- libobjc2 (GNU Objective-C)
- Compatible with NeXTSTEP semantics
- Enables cross-platform code

### 4. Distributed Objects (DO)
```
Object A (Task 1)
  ↓
Proxy (Mach IPC)
  ↓
Object B (Task 2)
[Method invocation over network/IPC]
```

Future UNHOX phase may integrate:
- Transparent remote method invocation
- Service discovery (like NeXT DO)
- Object serialization for IPC

## UNHOX References

Framework layer targets NeXTSTEP compatibility:

- `frameworks/AppKit/` — Window server and view hierarchy (libs-gui)
- `frameworks/Foundation/` — Core frameworks (libs-base)  
- `frameworks/objc-runtime/` — Objective-C runtime (libobjc2)
- `frameworks/libdispatch/` — GCD (Grand Central Dispatch)
- `docs/architecture.md` — System architecture (NeXT-inspired)

## See Also

- NeXT Computer: https://en.wikipedia.org/wiki/NeXT_Computer
- NeXTSTEP: https://en.wikipedia.org/wiki/NeXTSTEP
- OPENSTEP: https://en.wikipedia.org/wiki/OPENSTEP
- Objective-C: https://en.wikipedia.org/wiki/Objective-C
- macOS/iOS History: (evolution of NeXTSTEP)
- GNUstep: https://gnustep.github.io/ (open-source NeXTSTEP)

## License

NeXTSTEP documentation: NeXT proprietary (archived/out-of-print)
Preserved for historical and educational purposes.

## Preservation Note

NeXT Computer Inc. ceased operations in 1997. Apple acquired NeXT in 2001.
These documents are archived for:
- Historical preservation
- Educational reference
- Understanding modern macOS/iOS design heritage
- Open-source compatibility (GNUstep, etc.)

### OPENSTEP

- [ ] OPENSTEP Enterprise Developer's Guide
- [ ] OpenStep Specification (SunSoft/NeXT joint document)

### Interface Builder / Project Builder

- [ ] Interface Builder User Guide
- [ ] Project Builder User Guide

## Source URLs

- https://bitsavers.org/pdf/next/
- https://archive.org/search?query=nextstep+developer
- https://gnustep.org/resources/documentation.html (GNUstep mirrors)
