# NEOMACH — Future Core Systems

Design notes and adoption strategy for core system frameworks that NEOMACH
will need beyond the initial desktop milestone.  Each section addresses:

- What the framework is and why it matters
- Whether to write from scratch, adopt an existing open-source implementation,
  or build a clean-room reimplementation that is API-compatible with the
  corresponding Apple framework
- Licensing implications relative to NEOMACH's GPL-2.0 / LGPL-2.1 baseline
- Architectural fit with the Mach microkernel model

---

## Table of Contents

1. [Philosophy and Guiding Principles](#1-philosophy-and-guiding-principles)
2. [Kernel Enhancements — Mach Meets L4/seL4](#2-kernel-enhancements--mach-meets-l4sel4)
3. [Audio Subsystem](#3-audio-subsystem)
   - 3.1 Core Audio HAL
   - 3.2 Audio Units (AU) Plugin Architecture
   - 3.3 Core MIDI
   - 3.4 Audio Toolbox / AUGraph equivalents
4. [Graphics and Rendering](#4-graphics-and-rendering)
5. [Media Frameworks](#5-media-frameworks)
6. [Networking Frameworks](#6-networking-frameworks)
7. [Security and Cryptography](#7-security-and-cryptography)
8. [Foundation Layer Extensions](#8-foundation-layer-extensions)
9. [Language and Runtime Interoperability](#9-language-and-runtime-interoperability)
10. [Licensing and Compliance Matrix](#10-licensing-and-compliance-matrix)

---

## 1. Philosophy and Guiding Principles

### 1.1 Heritage and API Compatibility

NEOMACH is the Mach kernel reborn.  Its framework layer is intended to carry
the heritage of NeXTSTEP and OPENSTEP: the same clean Objective-C APIs, the
same port-centric service model, and the same philosophy that the kernel should
be minimal and servers should do the work.

**API heritage is not the same as code heritage.**  NEOMACH does not copy Apple
or NeXT source code.  The goal is that an application programmer familiar with
Apple's frameworks should feel at home — the method names, delegate patterns,
and data flow should be recognisable — without any line of Apple-proprietary
code appearing in the tree.

This approach has direct precedent:
- **GNUstep** has been reimplementing OpenStep APIs cleanly for thirty years
  under LGPL-2.1.
- **Wine** reimplemented Win32 APIs without copying Microsoft source.
- **ReactOS** reimplemented Windows kernel interfaces under GPL-2.0.
- **LLVM/Clang** reimplemented GCC front-end APIs.

### 1.2 Clean-Room vs. Adoption

For each subsystem NEOMACH faces three choices:

| Strategy | When to use |
|----------|-------------|
| **Write from scratch** | No suitable open-source equivalent exists, or the Mach IPC model requires a fundamentally different internal design |
| **Adopt existing open-source** | A GPL/LGPL-compatible implementation exists and fits the architecture; fork and integrate |
| **Clean-room reimplementation** | Apple framework's API must be targeted for compatibility, but no compatible implementation exists and Apple's source cannot be used |

### 1.3 Licensing Constraints

All code that ships in the NEOMACH kernel and servers must be license-compatible
with GPL-2.0-or-later (kernel) or LGPL-2.1-or-later (servers and frameworks).

Constraints:
- **Apache-2.0** is GPL-3.0-compatible but *not* GPL-2.0-compatible (the
  additional patent termination clauses are considered incompatible by the FSF).
  Apache-licensed code may appear as a *dynamically-linked optional component*
  or a submodule that is not linked into GPL-2.0 binaries.
- **MIT / BSD-2 / BSD-3** are compatible with both GPL-2.0 and LGPL-2.1.
- **APSL 2.0** (Apple Public Source License) is not GPL-2.0-compatible.
  No Apple-licensed source may be linked into NEOMACH kernel or server binaries.
- **LGPL-2.1** framework code (GNUstep) must be dynamically linked so
  end users can relink with modified versions.

> **Rule of thumb**: when in doubt, write it from scratch under GPL-2.0 or
> LGPL-2.1.  The cost of a clean implementation is far lower than the cost of
> a licensing dispute.

---

## 2. Kernel Enhancements — Mach Meets L4/seL4

The original CMU Mach 3.0 design is the architectural foundation of NEOMACH.
However, thirty years of microkernel research — particularly L4 and seL4 — have
produced concrete improvements that NEOMACH should incorporate.  These are
kernel-internal improvements: the Mach API surface stays compatible.

### 2.1 Synchronous IPC Endpoints (L4 Influence)

CMU Mach uses asynchronous port-based message queues.  L4 and its descendants
(Fiasco.OC, seL4, Nova) use synchronous IPC rendezvous: sender and receiver
rendezvous at a well-known endpoint and the message transfer occurs inline,
with the kernel scheduling directly from sender to receiver (handoff scheduling)
to minimise context switch overhead.

**NEOMACH approach**: Keep the Mach async port model as the user-visible API.
Internally, high-frequency RPC calls (BSD server syscall paths) will use a
synchronous fast-path when sender and receiver are both runnable:

```
1. Sender calls mach_msg(SEND | RECEIVE, rpc_port, msg)
2. Kernel checks if server thread is blocked in RECEIVE on rpc_port
3. If yes → direct message transfer into server's buffer, switch to server
4. If no  → queue message and block sender as usual
```

This gives L4-class IPC latency for the common case without changing the
external API.

### 2.2 Capability-Based Security (seL4 Influence)

seL4 formalises capabilities as the sole access-control mechanism: there are no
ACLs, no user IDs, no ambient authority — only capability tokens.  CMU Mach
almost got there with port rights, but still allows ambient authority (tasks can
look up services by name through the bootstrap server without holding a prior
capability).

**NEOMACH approach**: Introduce a *capability derivation* model layered over
Mach port rights.  Services that are security-sensitive will require a prior
capability port before granting a service port.  The bootstrap server itself
will only hand out capabilities to tasks that were granted access at boot time
by a policy server.

This is implemented entirely in userspace policy servers; the kernel does not
change.

### 2.3 Scheduling — Real-Time Priority Inheritance

CMU Mach's round-robin scheduler is insufficient for audio and real-time work.
NEOMACH will adopt a priority-inheritance scheduler similar to L4's policy-free
scheduling mechanism:

- Fixed-priority real-time threads (audio driver, MIDI server) run at
  RT priority classes.
- Priority inheritance prevents priority inversion across IPC: when a
  low-priority server holds a lock needed by a high-priority caller, the
  server temporarily inherits the caller's priority.
- Thread affinity and CPU pinning for NUMA/multi-core.

The scheduler lives in `kernel/kern/sched.c`.  The RT extensions will be
added as Phase 3 work.

### 2.4 Memory Protection Domains (seL4 UTS Influence)

seL4 introduces *untypedmemory* and formal capability-typed memory.  NEOMACH
will not go that far, but will introduce:

- **Protection domains**: groups of tasks that share a common capability space
  boundary.  Analogous to Android's zygote model.
- **Guard pages**: mandatory unmapped pages between kernel and user stacks.
- **ASLR**: Address-space layout randomisation for all tasks.

---

## 3. Audio Subsystem

Audio is one of the most demanding subsystems in any desktop OS.  Apple's
Core Audio architecture is exceptionally well-designed — it achieves
professional low-latency performance while maintaining a clean API.  NEOMACH
must be compatible with Core Audio / Core MIDI / Audio Units *by API design*
while not incorporating any Apple source code.

### 3.1 Core Audio HAL (Hardware Abstraction Layer)

Apple's CoreAudio HAL is a daemon (`coreaudiod`) that owns audio hardware and
provides a Mach-IPC-based interface to it.  NEOMACH's HAL server will follow
the same architecture natively, since everything in NEOMACH speaks Mach IPC.

**Strategy**: Write from scratch.

**Design**:

```
┌──────────────────────────────────────────────────────────────────────┐
│  Application Layer                                                   │
│  (AudioUnit graphs, AVAudioEngine, NSSound)                          │
└─────────────┬───────────────────────────────────────┬────────────────┘
              │ NeoAudio client API (Objective-C)      │
              ▼                                        ▼
┌─────────────────────────┐            ┌──────────────────────────────┐
│  NeoAudioEngine server  │◄──Mach IPC─►│  MIDI server (NeoMIDI)      │
│  (servers/audio/)       │            │  (servers/midi/)             │
└─────────┬───────────────┘            └──────────────────────────────┘
          │ Mach IPC
          ▼
┌─────────────────────────┐
│  Audio HAL server       │
│  (servers/audio/hal/)   │
└─────────┬───────────────┘
          │ Device IPC
          ▼
┌─────────────────────────┐
│  Device Server          │
│  (servers/device/)      │
│  ┌──────┐  ┌──────────┐ │
│  │ HDA  │  │ USB Audio│ │
│  └──────┘  └──────────┘ │
└─────────────────────────┘
```

The Audio HAL server is responsible for:
- Enumerating audio devices (via the device server)
- Managing audio I/O streams: sample rate, buffer size, channel layout
- Mixing and routing between streams
- Providing real-time I/O callbacks to client tasks via Mach port notifications
- Managing system audio policy (default output, privacy controls)

The API surface mirrors Apple's `AudioHardware.h` / `AudioHardwareBase.h`
public interface.  Header files will be written clean-room based on Apple's
published documentation and the open-source reference implementations below.

**Reference implementations** (read for design; do not copy GPL code into
NEOMACH unless the license is compatible):
- JACK2 (LGPL-2.1): real-time audio server; HAL design is instructive
- PipeWire (MIT): modern replacement for JACK/PulseAudio; MIT licensed — can
  be adopted or linked against
- PulseAudio (LGPL-2.1): reference for device management architecture

**LGPL-2.1 note**: JACK2 and PulseAudio code may be *linked dynamically* as
optional backends.  They must not be statically compiled into the NeoAudio
server binary if that binary will be GPL-2.0-only.  Use PipeWire (MIT) for any
code that is statically linked.

#### Key API types (clean-room headers)

```c
/* NeoAudio/AudioHardwareBase.h */
typedef UInt32 AudioObjectID;
typedef UInt32 AudioClassID;
typedef UInt32 AudioObjectPropertySelector;
typedef UInt32 AudioObjectPropertyScope;
typedef UInt32 AudioObjectPropertyElement;

typedef struct AudioObjectPropertyAddress {
    AudioObjectPropertySelector mSelector;
    AudioObjectPropertyScope    mScope;
    AudioObjectPropertyElement  mElement;
} AudioObjectPropertyAddress;

/* Primary entry point — mirrors CoreAudio semantics exactly */
extern kern_return_t AudioObjectGetPropertyData(
    AudioObjectID                       inObjectID,
    const AudioObjectPropertyAddress*   inAddress,
    UInt32                              inQualifierDataSize,
    const void*                         inQualifierData,
    UInt32*                             ioDataSize,
    void*                               outData);
```

These headers are written by the NEOMACH project under GPL-2.0 / LGPL-2.1.
They describe an interface, not an implementation; interfaces are not
copyrightable under *Oracle v. Google* (US courts) and similar rulings.

### 3.2 Audio Units (AU) Plugin Architecture

Audio Units are Apple's audio plugin format.  The equivalent in the open-source
world is **LV2** (ISC license) or **LADSPA** (LGPL-2.1).  NEOMACH will support
both a native NeoAU format and an LV2 host/plugin bridge.

**Strategy**: Write NeoAU host from scratch; adopt LV2 SDK (ISC) for plugin
format.

**Design**:

An Audio Unit in NEOMACH is a Mach task that registers itself with the
NeoAudioEngine server as a processing node.  The plugin lifecycle:

1. Host allocates an AU instance port (capability from NeoAudioEngine)
2. Host sends `kAudioUnitRequest_Initialize` → plugin allocates buffers
3. Per render cycle: host sends render request with I/O buffer ports
4. Plugin DSP runs in its own task, isolated by Mach VM boundaries
5. Plugin signals render-complete via Mach port notification
6. Host reads output buffer, passes to next node in the graph

The isolation model (one Mach task per plugin) gives stronger fault isolation
than macOS's in-process AU v2 format, while matching the intent of AU v3
(out-of-process plugins introduced in macOS 10.11).

**Key source references**:
- LV2 specification + lv2 SDK (ISC): `https://lv2plug.in/`
- JUCE framework plugin hosting code (GPL-3.0 + commercial — read only,
  do not copy)
- Steinberg VST3 SDK (GPL-3.0): read only for API shape

#### NeoAU registration (planned API)

```objc
/* NeoAudio/AudioComponent.h */
@interface NMAudioComponent : NSObject
@property (readonly) NSString *type;         /* e.g. "aufx" */
@property (readonly) NSString *subtype;      /* e.g. "lpas" */
@property (readonly) NSString *manufacturer; /* e.g. "NMCH" */
@property (readonly) NSString *name;
- (id<NMAudioUnit>)instantiateWithOptions:(NMAudioComponentInstantiationOptions)options
                                    error:(NSError **)error;
@end
```

### 3.3 Core MIDI

Core MIDI provides:
- MIDI device enumeration and hot-plug detection
- Virtual MIDI ports between applications
- MIDI Thru routing
- MIDI System Exclusive (SysEx) message handling
- Network MIDI (RTP-MIDI / MIDI over IP)

**Strategy**: Write NeoMIDI from scratch as a Mach IPC server.

**Reference implementations** (design reference, not code copy):
- ALSA Sequencer (LGPL-2.1): defines a mature MIDI event model
- RtMidi (MIT): portable MIDI I/O; can be adopted or used as a model
- jack-midi (LGPL-2.1): MIDI over JACK

**ALSA note**: ALSA itself (kernel driver) is GPL-2.0.  The ALSA userspace
library (`libasound`) is LGPL-2.1 and may be linked dynamically.  RtMidi (MIT)
is preferable for any statically-linked component.

**Design**:

```
┌─────────────────────────────────────────────────────────────┐
│  NeoMIDI server  (servers/midi/)                             │
│                                                             │
│  ┌─────────────────┐    ┌──────────────────────────────┐    │
│  │ Device registry  │    │ Virtual port manager          │    │
│  │ (USB MIDI, BT   │    │ (app-to-app MIDI routing)    │    │
│  │  MIDI, IAC)     │    └──────────────────────────────┘    │
│  └────────┬────────┘                                        │
│           │ Mach IPC                                        │
│           ▼                                                 │
│  ┌────────────────────┐   ┌────────────────────────────┐    │
│  │ USB MIDI driver    │   │ RTP-MIDI (Network MIDI)    │    │
│  │ (Device Server)    │   │ (Network Server)           │    │
│  └────────────────────┘   └────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

The NeoMIDI public API mirrors `CoreMIDI/CoreMIDI.h` selector names and
delegate patterns.  Headers are written clean-room.

### 3.4 Audio Toolbox / AUGraph Equivalent

Apple's `AudioToolbox` provides `AUGraph`, `ExtAudioFile`, `AudioConverter`, and
`MusicPlayer` (MIDI sequencer).  NEOMACH will implement:

| Apple class/function | NEOMACH equivalent | Strategy |
|----------------------|--------------------|----------|
| `AUGraph` | `NMAUGraph` | Write from scratch |
| `ExtAudioFile` | `NMExtAudioFile` | Write from scratch |
| `AudioConverter` | `NMAudioConverter` | Adopt libsamplerate (BSD-2) + FFmpeg libavcodec (LGPL-2.1) |
| `MusicPlayer` | `NMMusicPlayer` | Write from scratch; use RtMidi for MIDI I/O |
| `AUSampler` | `NMSamplerUnit` | Adopt FluidSynth (LGPL-2.1) as backend |
| `AudioFileStream` | `NMAudioFileStream` | Adopt libsndfile (LGPL-2.1) |

**FluidSynth** (LGPL-2.1): may be dynamically linked into the NeoAudioEngine
server.  Provides a high-quality SF2/SF3 sampler that maps directly onto the
`AUSampler` use case.

---

## 4. Graphics and Rendering

### 4.1 Core Graphics (Quartz 2D)

Core Graphics (`CGContext`, `CGPath`, `CGImage`, `CGColor`) is Apple's 2D
drawing API, descended from the Display PostScript model that NeXTSTEP used.

**Strategy**: Adopt **Cairo** (LGPL-2.1) as the rendering backend; write a
thin `CoreGraphics/` Objective-C wrapper layer on top.

Cairo implements all of the Quartz 2D primitives (paths, gradients, text,
images, transforms, blend modes) and is available under LGPL-2.1.  GNUstep's
`NSBezierPath` and `NSGraphicsContext` already use Cairo as their backend —
NEOMACH can build directly on top of that layer.

The `CoreGraphics/` framework will export a C API (`CGContext*`, `CGPath*`, ...)
that maps directly onto Cairo internals.

### 4.2 Core Animation (CA)

Core Animation provides:
- Layer tree compositing
- Implicit and explicit animations
- Metal-backed layer rendering (on Apple platforms)

**Strategy**: Write `NMAnimation` from scratch as a Mach IPC server attached to
the NEOMACH Display Server.

The Display Server (DPS-inspired compositor, `frameworks/DisplayServer/`) will
accept layer tree commands over Mach IPC and perform compositing in a dedicated
GPU-accelerated task.  The CA-equivalent API will be:

```objc
@interface NMLayer : NSObject
@property CGRect frame;
@property float opacity;
@property NSArray<NMLayer *> *sublayers;
- (void)addAnimation:(NMAnimation *)anim forKey:(NSString *)key;
@end
```

### 4.3 Metal-Inspired GPU Compute

Metal is Apple's low-level GPU API.  NEOMACH's equivalent will be built around
the open-source GPU ecosystem:

- **Vulkan** (Apache-2.0 headers, royalty-free spec): industry-standard GPU
  compute and graphics API.  Vulkan drivers (Mesa, RADV, ANV) are MIT/MIT-like.
- **Mesa** (MIT / LGPL-2.1): open-source GPU drivers for AMD (RADV), Intel
  (ANV, iris), and software rasterisation (llvmpipe, softpipe).

The NEOMACH Metal equivalent will present a Vulkan-backed `NMMetal*` API, with
a shader language based on GLSL/SPIR-V rather than Metal Shading Language.
Long-term, an MSL → SPIR-V transpiler (via `spirv-cross`, Apache-2.0) could
allow upstream Metal shader code to run on NEOMACH with recompilation.

---

## 5. Media Frameworks

### 5.1 Core Video

Core Video manages:
- CVPixelBuffer / CVImageBuffer — shared video frame memory
- CVDisplayLink — display sync callbacks
- Metal texture bridging for video frames

**Strategy**: Write `NMCoreVideo` from scratch.  Use shared Mach memory objects
(`vm_allocate` + `mach_memory_object`) for zero-copy frame sharing between
the capture server, codec server, and display server.

### 5.2 Core Media / CMSampleBuffer

Core Media defines the media sample type hierarchy used by AVFoundation:
`CMSampleBuffer`, `CMFormatDescription`, `CMTime`, `CMTimeRange`.

**Strategy**: Write from scratch under LGPL-2.1.  The type system is purely
structural (C structs + CF types); clean-room implementation is straightforward.
`CMTime` in particular is a well-specified rational time type with no
non-obvious behaviour.

### 5.3 AVFoundation Equivalents

AVFoundation is Apple's high-level media framework.  Equivalent functionality:

| Apple class | NEOMACH class | Backend |
|-------------|--------------|---------|
| `AVAudioEngine` | `NMAudioEngine` | NeoAudio server |
| `AVAudioPlayer` | `NMAudioPlayer` | NeoAudio + libsndfile |
| `AVAudioRecorder` | `NMAudioRecorder` | NeoAudio HAL |
| `AVPlayer` | `NMPlayer` | FFmpeg (LGPL-2.1) |
| `AVAsset` | `NMAsset` | FFmpeg demuxers (LGPL-2.1) |
| `AVCaptureSession` | `NMCaptureSession` | V4L2 / USB UVC camera server |
| `AVSpeechSynthesizer` | `NMSpeechSynthesizer` | flite (BSD-like) / eSpeak-NG (GPL-3.0) |

**FFmpeg**: The core FFmpeg libraries (`libavcodec`, `libavformat`,
`libavutil`, `libswresample`) are available under LGPL-2.1-or-later when built
without GPL-only components.  NEOMACH will use them as dynamically-linked
codec/demuxer backends.  GPL-only components (certain codecs, e.g. x264 when
used via GPL build) must be isolated in a separate process with a Mach IPC
boundary, maintaining LGPL compliance for the framework layer.

---

## 6. Networking Frameworks

### 6.1 Network.framework Equivalent

Apple's Network.framework provides:
- Connection-oriented TCP/UDP/QUIC endpoints
- TLS via Security.framework
- Path monitoring (network reachability)
- Bonjour/DNS-SD service discovery

**Strategy**: Write `NMNetwork` as an Objective-C wrapper over the NEOMACH
network server.  The network server already owns the BSD socket API; NMNetwork
adds a higher-level `NWConnection`-style API.

TLS: use **mbedTLS** (Apache-2.0, dynamically linked) or **LibreSSL** (ISC +
OpenSSL, permissive).

QUIC: **quiche** (BSD-2-Clause) or **lsquic** (MIT) as a userspace library.

### 6.2 CFNetwork / NSURLSession Equivalent

CFNetwork provides the URL loading system.  GNUstep Base already implements
`NSURLSession`, `NSURLRequest`, and `NSURLResponse` under LGPL-2.1.  NEOMACH
will build on top of GNUstep Base's implementation, adding the network server
integration.

### 6.3 Bonjour / mDNS

Apple's Bonjour is based on the DNS-SD and mDNS specifications (RFC 6762,
RFC 6763).  The reference implementation, `mDNSResponder`, is available from
Apple under APSL 2.0 — **not GPL-compatible**.

**Strategy**: Use **Avahi** (LGPL-2.1) — the dominant Linux mDNS/DNS-SD
daemon — as the NEOMACH mDNS server.  Avahi is LGPL-2.1 and is already widely
used as a Bonjour-compatible implementation.

---

## 7. Security and Cryptography

### 7.1 Security Framework / Keychain

The Keychain stores secrets (passwords, keys, certificates) with access control
enforced by capabilities.  In NEOMACH this maps naturally onto Mach port rights:
a keychain item is accessed by presenting a capability port, not by knowing a
user ID.

**Strategy**: Write `NMKeychain` from scratch as a Mach server
(`servers/keychain/`).  Store encrypted secrets using **libsodium** (ISC) for
the cryptographic primitives.

### 7.2 CryptoKit Equivalent

Apple's CryptoKit provides Swift-idiomatic cryptography (AES-GCM, ChaCha20,
ECDH, Ed25519, SHA family).

**Strategy**: Write `NMCrypto` as an Objective-C wrapper over **libsodium**
(ISC) for symmetric and authenticated encryption, and **BearSSL** (MIT) or
**mbedTLS** (Apache-2.0, dynamically linked) for TLS and certificate handling.

### 7.3 Secure Enclave / TEE

Apple silicon has a Secure Enclave.  For NEOMACH on ARM, the equivalent will be
OP-TEE (BSD-2-Clause) for TrustZone-capable hardware.

---

## 8. Foundation Layer Extensions

### 8.1 Core Data Equivalent

Core Data is an object graph persistence framework.  GNUstep does not have a
complete Core Data equivalent.

**Strategy**: Write `NMData` from scratch under LGPL-2.1 using **SQLite**
(public domain) as the backing store.  The API will mirror
`NSManagedObjectContext`, `NSManagedObject`, `NSPersistentStore`, and
`NSFetchRequest`.

### 8.2 Core Spotlight Equivalent

Core Spotlight provides full-text search over application content.

**Strategy**: Adopt **Xapian** (GPL-2.0-or-later) as the indexing backend,
exposing a `NMSpotlight*` API.  Xapian is GPL-2.0 and is compatible with
NEOMACH's kernel license.

### 8.3 UserNotifications

User-facing notifications routed through a notification daemon.

**Strategy**: Write `NMNotificationCenter` as an extension of GNUstep's
`NSDistributedNotificationCenter`, adding persistence and display integration
with the workspace manager.

### 8.4 CloudKit / Distributed Data

Distributed sync is out of scope for v1.0 but worth noting:  GNUstep's
`NSDistributedNotificationCenter` and a future NMSync server built over the
network server will fill this role.  No Apple cloud infrastructure will be used
or implied.

---

## 9. Language and Runtime Interoperability

### 9.1 Swift Runtime

Apple's Swift standard library (`swift-stdlib`) is available under Apache-2.0,
which is **not GPL-2.0-compatible for static linking**.  Dynamic linking of
Swift runtime libraries into LGPL-2.1 framework code is permissible.

**Short-term**: NEOMACH is primarily an Objective-C / C project.  Swift support
is deferred until the framework layer is stable.

**Long-term**: Run Swift code in userspace tasks with the Swift stdlib
dynamically linked.  The runtime interaction with Mach (thread creation, memory
allocation) will go through NEOMACH's POSIX layer.

### 9.2 Objective-C Runtime

NEOMACH uses **libobjc2** (MIT, at `frameworks/objc-runtime/`).  This is the
GNUstep Objective-C runtime, which supports modern Objective-C features
(ARC, blocks, fast messaging) and is MIT-licensed, fully compatible with
GPL-2.0 and LGPL-2.1.

### 9.3 Blocks / libdispatch

Apple's `swift-corelibs-libdispatch` is Apache-2.0.  It is included as a
submodule (`frameworks/libdispatch/`) and must be dynamically linked.

An alternative is **kqueue + workqueue** implemented directly over Mach port
notifications, which would be GPL-2.0-clean.  This is the preferred long-term
path for the kernel-linked portions of GCD; the Apache-licensed libdispatch
remains available as a userspace shared library.

---

## 10. Licensing and Compliance Matrix

Summary of adoption decisions for all planned frameworks:

| Subsystem | Strategy | Upstream | License | Link mode |
|-----------|----------|----------|---------|-----------|
| Core Audio HAL | Write from scratch | — | GPL-2.0 / LGPL-2.1 | — |
| Audio Units host | Write from scratch | — | LGPL-2.1 | — |
| LV2 plugin support | Adopt | lv2plug.in/lv2 | ISC | Static or dynamic |
| Core MIDI | Write from scratch | — | LGPL-2.1 | — |
| SoundFont sampler | Adopt | FluidSynth | LGPL-2.1 | Dynamic |
| Audio file I/O | Adopt | libsndfile | LGPL-2.1 | Dynamic |
| Sample rate conversion | Adopt | libsamplerate | BSD-2 | Static or dynamic |
| Video decode/encode | Adopt | FFmpeg (LGPL build) | LGPL-2.1 | Dynamic |
| Core Graphics | Adopt + wrap | Cairo | LGPL-2.1 | Dynamic |
| Core Animation | Write from scratch | — | LGPL-2.1 | — |
| GPU compute (Metal equiv.) | Adopt | Mesa + Vulkan | MIT/LGPL-2.1 | Dynamic |
| Core Video frame mgmt | Write from scratch | — | LGPL-2.1 | — |
| Core Media types | Write from scratch | — | LGPL-2.1 | — |
| Network.framework equiv. | Write from scratch | — | LGPL-2.1 | — |
| TLS | Adopt | LibreSSL | ISC | Dynamic |
| QUIC | Adopt | quiche | BSD-2 | Dynamic |
| mDNS / Bonjour | Adopt | Avahi | LGPL-2.1 | Dynamic |
| Keychain / credentials | Write from scratch | — | LGPL-2.1 | — |
| Crypto primitives | Adopt | libsodium | ISC | Static or dynamic |
| TrustZone / TEE (ARM) | Adopt | OP-TEE | BSD-2 | Dynamic |
| Core Data equiv. | Write from scratch | SQLite backend | LGPL-2.1 | — |
| Full-text search | Adopt | Xapian | GPL-2.0+ | Dynamic |
| Objective-C runtime | Adopt (submodule) | libobjc2 | MIT | Dynamic |
| GCD / libdispatch | Adopt (submodule) | swift-corelibs | Apache-2.0 | Dynamic only |
| Foundation | Adopt (submodule) | GNUstep Base | LGPL-2.1 | Dynamic |
| AppKit | Adopt (submodule) | GNUstep GUI | LGPL-2.1 | Dynamic |
| Speech synthesis | Adopt | flite | BSD-like | Dynamic |

### 10.1 Rules for GPL-2.0 Kernel Code

The NEOMACH kernel (`kernel/`) is GPL-2.0-only.  Nothing Apache-2.0 or
GPL-3.0-only may be compiled into the kernel image.  All kernel dependencies
must be MIT, BSD-2, BSD-3, ISC, or GPL-2.0-compatible.

### 10.2 Rules for LGPL-2.1 Server and Framework Code

NEOMACH servers (`servers/`) and frameworks (`frameworks/`) are LGPL-2.1.
Apache-2.0 code (libdispatch, Vulkan headers) must be dynamically linked.
GPL-3.0-only code must not appear at all in the distributed binaries — it may
only be consulted as a design reference.

### 10.3 Clean-Room API Headers

The `NeoAudio/`, `NeoMIDI/`, `NMCoreVideo/`, and related framework headers
shipped with NEOMACH are original works by the NEOMACH project under LGPL-2.1.
They describe interfaces inspired by Apple's published documentation and are not
copies of Apple's SDK headers.  Functional API compatibility with Apple
frameworks is the goal, not source-level identity.

---

*Document status: draft — reviewed against GPL-2.0 / LGPL-2.1 constraints as of 2026-03.*

*Next steps: open individual RFCs in `docs/rfcs/` for each subsystem before
implementation begins.  See `docs/roadmap.md` for the milestone sequence.*
