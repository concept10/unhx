# RFC-0005: Audio Subsystem — Core Audio, Core MIDI, and Audio Units for UNHOX

- **Status**: Proposed
- **Author**: UNHOX Project
- **Date**: 2026-03-06
- **Phase**: 5
- **Supersedes**: none
- **Superseded-by**: none

## Summary

This RFC proposes the architecture for Core Audio-equivalent, Core MIDI-equivalent,
and Audio Units functionality in UNHOX.  Following strict Mach microkernel discipline,
**no audio logic lives in the kernel**.  An Audio Server and a MIDI Server run as
ordinary userspace tasks and communicate with hardware drivers, each other, and client
applications exclusively through Mach port IPC.  The Audio Units plugin framework
allows effects, instruments, and format converters to run as isolated tasks, with
crash containment guaranteed by the Mach capability model.

The kernel gains exactly **one** new primitive to support audio: a real-time
scheduling policy (`SCHED_RT`) whose design is specified in the pending Kernel
Verification RFC (PR #15).  All other required primitives — OOL memory descriptors
for zero-copy buffer sharing and port right transfer for inter-server handoff — are
the same gate conditions already identified in the Display Server Alternative RFC
(PR #9) and the Display Server Architecture RFC (PR #11), and are being implemented
as part of the IPC Architecture work (PR #8).

> **Note on RFC numbering.**  RFC-0001 (IPC Message Format) is merged.  Four additional
> proposals are open simultaneously, each claiming RFC-0002 at branch creation time.
> This document is numbered RFC-0005 to sit clearly above all competing proposals.
> Final sequential numbers will be assigned at merge time following the process in
> `docs/rfcs/README.md` (PR #15).

## Motivation

Audio on modern systems has three hard requirements that must be addressed at the
architecture level before implementation begins:

1. **Low latency** — a 128-frame hardware buffer at 48 kHz gives 2.67 ms per period.
   The audio I/O callback must fire with sub-millisecond jitter.  This is a scheduling
   problem, not an IPC problem.  The kernel's `SCHED_RT` policy (RFC pending, PR #15)
   provides the required guarantee; everything else stays in userspace.

2. **Extensibility** — audio effects, synthesizers, and format converters must be
   loadable at runtime.  The plugin model (Audio Units) is a userspace concern and
   requires no kernel changes beyond the IPC primitives already being implemented for
   the display server (PRs #9, #11).

3. **Isolation** — a misbehaving plugin must not crash the system.  Running each
   plugin as a separate Mach task and communicating via port rights achieves this
   without a new kernel mechanism: `MACH_NOTIFY_NO_SENDERS` already handles clean-up
   when an Audio Unit task dies.

This RFC documents the design decisions that satisfy all three requirements within
UNHOX's Mach architecture, and explicitly identifies which other pending RFCs and
PRs must land first.

## Dependencies on Other Pending RFCs and PRs

The audio subsystem shares gate conditions with the Display Server (PRs #9, #11).
The conditions are restated here for completeness; they are defined and tracked in
those RFCs.

### AUDIO-IMP-1 — OOL Memory Descriptors (identical to IPC-IMP-1 in PR #9)

Sample data is too large to copy inline.  A stereo 128-frame float buffer is 1 KB;
a 4096-frame buffer is 32 KB.  Mach OOL descriptors (`MACH_MSG_OOL_DESCRIPTOR`)
remap pages without copying.  This is the same capability required by the display
server for pixel buffer transfer.

**Implementation**: `kernel/ipc/ipc_kmsg.c`, `kernel/vm/vm_map.c`.
**Test**: `tests/ipc/ipc_ool_test.c` (shared with display server gate test).

### AUDIO-IMP-2 — Port Right Transfer in Messages (identical to IPC-IMP-2 in PR #9)

Audio Unit port handoff requires `MACH_MSG_PORT_DESCRIPTOR` in complex messages.
The Audio Server must pass send rights to newly created Audio Unit ports to client
tasks.  The display server requires the same mechanism for event port delivery.

**Implementation**: `kernel/ipc/ipc_kmsg.c` copy-out, `kernel/ipc/ipc_right.c`
`MOVE_SEND` / `COPY_SEND` semantics.  Tracked in PR #8 (IPC Architecture).

### AUDIO-IMP-3 — Blocking Receive with Timeout (identical to IPC-IMP-3 in PR #9)

Audio Unit render loops must block waiting for a render request, then time out at
period end to handle overruns.  `MACH_RCV_TIMEOUT` semantics in `ipc_mqueue.c`
are required.  Tracked in PR #8 (IPC Architecture).

### AUDIO-IMP-4 — Bootstrap Server Port Lookup (identical to IPC-IMP-4 in PR #9)

Clients must look up `com.unhox.audio.server` and `com.unhox.midi.server` at
runtime via the Bootstrap Server.

**Implementation**: `servers/bootstrap/` (Phase 1, tracked in TASKS.md).

### AUDIO-IMP-5 — `SCHED_RT` Real-Time Scheduling Policy (audio-specific)

The Audio Server's I/O thread must be promoted to real-time priority.  This requires
the `SCHED_RT` policy with a `(period, computation, deadline)` triple, modelled on
OSF MK6 and Tevanian et al.  Design is specified in the pending Kernel Verification
RFC (PR #15); the IPC capability safety and memory isolation proofs in that RFC apply
directly to the audio server's threat model.

**Implementation**: `kernel/kern/sched.c`, `kernel/kern/kern.h`,
`kernel/ipc/ipc_mqueue.c` (priority inheritance).
**Phase**: 2 (tied to Kernel Verification RFC Phase 2 milestones).

## Design

### Architecture

```
Client Application
    │
    │  mach_msg(com.unhox.audio.server)
    ▼
Audio Server                  (userspace, SCHED_RT I/O thread)
    │
    ├── MIDI Server           (com.unhox.midi.server)
    ├── Audio Unit tasks      (per-plugin tasks, OOL buffers)
    └── Device Server         (HDA / USB audio / virtio-sound drivers)
    │
    ▼
Mach Kernel  (IPC · VM · SCHED_RT · Tasks · Threads)
```

The kernel/server boundary is unchanged from the principles in
`docs/architecture.md`.  The audio tier sits between the Device Server
(Phase 3) and the Framework layer (Phase 4+), and is consistent with the
display server tier already documented in PRs #9 and #11.

### Kernel Changes

The kernel requires only `SCHED_RT` (AUDIO-IMP-5 above).  New fields in
`struct thread` (`kernel/kern/kern.h`):

```c
sched_policy_t  th_sched_policy;   /* SCHED_NORMAL | SCHED_RT */
uint64_t        th_rt_period;      /* OSF MK6 RT triple — period     */
uint64_t        th_rt_computation; /*                     computation */
uint64_t        th_rt_deadline;    /*                     deadline    */
```

An `SCHED_RT` thread preempts all `SCHED_NORMAL` threads the moment its
period fires.  Priority inheritance on `ipc_mqueue` wakeup propagates
the RT priority through every hop in the audio graph, preventing priority
inversion across Audio Unit task boundaries.

Reference: OSF MK6 `kern/sched_prim.c`; Mach 3.0 Kernel Principles §6.3.

### Shared-Memory Audio Buffers

Zero-copy buffer sharing reuses Mach OOL descriptors (defined in RFC-0001,
implemented in AUDIO-IMP-1).  No new kernel primitive is needed:

```c
struct au_render_request {
    mach_msg_header_t          header;
    mach_msg_body_t            body;
    mach_msg_ool_descriptor_t  buffer;    /* VM region — zero copy        */
    uint64_t                   timestamp; /* Mach absolute time           */
    uint32_t                   frame_count;
    uint32_t                   channel_count;
    uint32_t                   format;    /* AUDIO_FMT_F32_LE             */
};
```

### Audio Server (`servers/audio/`)

The Audio Server is the HAL (Hardware Abstraction Layer).  It owns:
device management, per-client stream sessions, the audio graph DAG,
the `SCHED_RT` I/O render thread, and auto-inserted format conversion.

Key IPC operations (message IDs 8000–8299):

| ID   | Operation | Description |
|------|-----------|-------------|
| 8001 | `AUDIO_LIST_DEVICES` | Enumerate audio devices from Device Server |
| 8101 | `AUDIO_OPEN_OUTPUT` | Allocate stream port + OOL ring buffer |
| 8102 | `AUDIO_CLOSE` | Tear down stream session |
| 8103 | `AUDIO_OPEN_INPUT` | Microphone / capture stream |
| 8201 | `GRAPH_ADD_NODE` | Instantiate an Audio Unit task |
| 8202 | `GRAPH_REMOVE_NODE` | Terminate an Audio Unit task |
| 8203 | `GRAPH_CONNECT` | Connect AU output bus to another AU input |
| 8204 | `GRAPH_DISCONNECT` | Remove a connection |
| 8205 | `GRAPH_START` | Begin RT rendering |
| 8206 | `GRAPH_STOP` | Halt RT rendering |

### MIDI Server (`servers/midi/`)

The MIDI Server manages USB MIDI devices (via Device Server), legacy
MPU-401 UART, virtual MIDI endpoints, and timestamped event routing.

Key IPC operations (message IDs 8300–8499):

| ID   | Operation | Description |
|------|-----------|-------------|
| 8301 | `MIDI_LIST_DEVICES` | Enumerate MIDI devices |
| 8302 | `MIDI_CREATE_VIRTUAL` | Create virtual source/destination endpoint |
| 8401 | `MIDI_EVENT` | Deliver a timestamped 3-byte MIDI event |
| 8402 | `MIDI_SYSEX` | Deliver a SysEx message (OOL descriptor) |

Each MIDI event carries a Mach absolute time so the Audio Server can
convert it to a sample-accurate frame offset within the current render
period.

### Audio Units (`frameworks/AudioUnits/`)

Each Audio Unit exposes three Mach ports:

- `au_render_port` — Audio Server sends a render request (OOL buffer)
  each hardware period and awaits a reply.
- `au_midi_port` — MIDI Server delivers timestamped events here for
  instrument and MIDI-reactive effect units.
- `au_control_port` — host sends parameter-change messages (gain, cutoff, etc.).

Unit types:

| Type | Description |
|------|-------------|
| `AU_TYPE_OUTPUT` | Final write to hardware via Audio Server |
| `AU_TYPE_MIXER` | N-input summing mixer |
| `AU_TYPE_EFFECT` | In-place DSP (EQ, reverb, dynamics) |
| `AU_TYPE_INSTRUMENT` | MIDI-driven synthesizer |
| `AU_TYPE_FORMAT_CONVERTER` | Sample-rate / channel-count conversion |

Message IDs 8500–8799 are reserved for the Audio Unit render and control
protocol (see `docs/audio-server-design.md` for full struct definitions).

### Audio Unit Design Lineage and AUv3 Compatibility

The UNHOX Audio Unit model is derived from Apple's Audio Unit plug-in API,
which has gone through three major versions:

| Version | Era | Hosting model | IPC |
|---------|-----|---------------|-----|
| **AU v1** | Mac OS X 10.0–10.1 (2001) | In-process Carbon Component Manager bundle | n/a (direct function call) |
| **AU v2** | Mac OS X 10.2–macOS 12 (2002–2021) | In-process Core Audio bundle (`AudioComponent`) | n/a (direct function call) |
| **AU v3** | iOS 9 / macOS 10.11+ (2015–present) | **Out-of-process App Extension**; XPC rendezvous | XPC (Mach IPC layer) |

**UNHOX Audio Units are structurally equivalent to AUv3**, not AU v2:

- Every third-party AU runs in a **separate Mach task** — analogous to the
  XPC Extension process that hosts an AUv3 plugin on macOS.
- Communication between the Audio Server and an AU uses Mach messages (OOL
  buffer render requests), just as macOS uses XPC messages in AUv3 hosting.
- Crash isolation (`MACH_NOTIFY_NO_SENDERS`) mirrors the process-boundary
  isolation that macOS AUv3 provides automatically via the XPC sandbox.

The functional differences from AUv3:

- **No App Extensions container**: UNHOX has no `NSExtension` bundle or app
  store sandboxing.  Plugins are plain Mach tasks registered with the Bootstrap
  Server under a well-known name (`com.unhox.au.<vendor>.<name>`).
- **No XPC**: The transport is native Mach IPC rather than XPC (which is just
  a Mach-based serialisation layer).  This removes one serialisation layer and
  reduces latency.
- **AUv3 binary compatibility** is a future goal (see Open Questions §6), not
  part of this RFC.  A thin AUv3 compatibility wrapper task that bridges
  `AudioUnitRemote` XPC calls to UNHOX AU Mach IPC is feasible but deferred.

**SCHED_RT as a catalyst for a dedicated kernel RFC.**  The `SCHED_RT`
real-time scheduling policy (AUDIO-IMP-5) is a first-class kernel feature
needed not only by audio but by any low-latency server (display vsync,
networking, real-time control).  Its scope justifies a dedicated RFC rather
than being a subsection of the audio RFC.  This RFC therefore **explicitly
recommends that a `RFC-0006-rt-scheduling.md` be opened** covering:

- The `SCHED_RT` / `SCHED_RT_CRITICAL` policy API and `thread_set_rt_policy()`
  interface (syscall vs. kernel task port message — see Open Questions §3).
- Priority inheritance protocol through `ipc_mqueue` wakeup paths, with
  seL4-style MCS (Mixed-Criticality Scheduling) as an optional upgrade path.
- Deadline-driven scheduling improvements beyond the OSF MK6 triple: EDF
  (Earliest Deadline First) or CBS (Constant Bandwidth Server) algorithms.
- Integration with the verification layers defined in PR #15: TLA+ model of
  the RT scheduler (Layer 3) and Isabelle/HOL proof of priority-inversion
  freedom (Layer 4).

Until RFC-0006 is opened and accepted, the `SCHED_RT` gate condition
(AUDIO-IMP-5) is tracked in this RFC's Implementation Checklist §Kernel.

### Plugin Format Compatibility Bridges (VST2, VST3, AAX)

UNHOX Audio Units can host third-party plugins written for other plugin
formats through **bridge tasks**.  Each bridge is a separate Mach task that:

1. Loads the native plugin binary using the platform dynamic linker.
2. Exposes a standard UNHOX AU render loop and Mach port set.
3. Translates between the native plugin SDK calls and the
   `au_render_request` / `au_midi_event_msg` / `au_set_param_msg` IPC
   messages defined in §Audio Units above.

This is the same architecture used by REAPER's `reaper_linux_vst_bridge`,
Wine `vst_bridge`, and macOS Rosetta-based VST bridging in AU hosts.

#### VST2 (Steinberg Virtual Studio Technology 2.x)

VST2 plugins expose a C ABI function table (`AEffect *effect`).  The bridge
calls `VSTPluginMain()`, allocates an `AEffect`, and wraps it:

```
┌──────────────────────────────┐
│  vst2_bridge task             │
│  ├── dlopen(plugin.vst2.so)  │
│  ├── AEffect *effect = …     │
│  ├── processReplacing()      │  ← called each UNHOX AU render request
│  └── UNHOX AU render loop   │  ← translates mach_msg ↔ VST2 C calls
└──────────────────────────────┘
         │  UNHOX AU IPC (8500–8799)
         ▼
    Audio Server
```

File: `servers/audio/vst2_bridge/vst2_bridge.c`

Licensing note: Steinberg VST2 SDK usage is under Steinberg VST2 Free SDK
licence.  VST2 plugin ABI is frozen; no further updates from Steinberg.

#### VST3 (Steinberg Virtual Studio Technology 3.x)

VST3 plugins use a COM-like C++ interface (`IComponent`, `IAudioProcessor`,
`IEditController`).  The bridge links against the open-source Steinberg
VST3 SDK (GPL v3 / Steinberg dual licence):

```
┌─────────────────────────────────────────────────────────────────┐
│  vst3_bridge task                                               │
│  ├── VST3 factory: IPluginFactory → IComponent + IAudioProcessor│
│  ├── IAudioProcessor::process() ← render request              │
│  ├── IEditController  ← parameter get/set                     │
│  └── UNHOX AU render loop  ← translates mach_msg ↔ VST3 C++  │
└─────────────────────────────────────────────────────────────────┘
         │  UNHOX AU IPC (8500–8799)
         ▼
    Audio Server
```

File: `servers/audio/vst3_bridge/vst3_bridge.cpp` (C++ required by VST3 SDK).

A C++ compiler and the Steinberg VST3 SDK must be present in the build tree.
The bridge is an **optional component** gated on `UNHOX_ENABLE_VST3_BRIDGE`
in CMake.

#### AAX (Avid Audio eXtension — Pro Tools)

AAX is Avid's proprietary format for Pro Tools HDX and Native.  The AAX SDK
is available only under a signed NDA with Avid.  A bridge is **architecturally
possible** (same task-isolation model as VST), but **cannot be implemented
in this repository** without the SDK.  An implementation requires:

- A signed AAX developer agreement with Avid.
- The AAX SDK headers and `AAX_CMonolithicParameters` base class.
- A separate out-of-tree repository, linked into the build as an optional
  external component.

UNHOX documents the architecture here so that a licensed developer can build
the bridge without redesigning the audio server.

#### LV2 (LADSPA Version 2 — Linux Audio)

LV2 is a fully open plugin format (ISC licence) with rich metadata via RDF
Turtle `.ttl` manifests.  The host API is C-compatible and self-describing.
LV2 is a strong candidate for the **first** bridge implementation:

- No NDA or proprietary SDK required.
- Many high-quality open-source plugins available (Calf, ZamaPlugins, x42).
- Atom event port maps naturally to UNHOX MIDI event messages.

File: `servers/audio/lv2_bridge/lv2_bridge.c`

#### Plugin Bridge Architecture Summary

| Format | SDK Licence | Binary ABI | UNHOX Bridge Status |
|--------|-------------|------------|---------------------|
| LV2 | ISC (open) | C | Planned — Phase 5 |
| VST2 | Steinberg Free | C | Planned — Phase 5 |
| VST3 | GPL v3 / Steinberg dual | C++ (COM-like) | Planned — Phase 5 (C++ build required) |
| AAX | Avid NDA | C++ (proprietary) | Architecture documented; implementation out-of-tree |
| CLAP | MIT (open) | C | Candidate for Phase 5 (newer, simpler than VST3) |

CLAP (CLever Audio Plug-in API) is a recent open alternative to VST3
(MIT licence, Bitwig/u-he/Surge) with a simpler C ABI and native
process-isolation model that maps well to UNHOX tasks.

### Port Naming Convention

Bootstrap port names follow the same `com.unhox.*` convention used by the
Display Server (PRs #9, #11):

| Name | Server |
|------|--------|
| `com.unhox.audio.server` | Audio Server service port |
| `com.unhox.midi.server` | MIDI Server service port |
| `com.unhox.audio.default_output` | Default output stream port |
| `com.unhox.audio.default_input` | Default input (mic) stream port |

### Message ID Registry

To avoid conflicts with the Display Server (message IDs 4000–4032 in PR #9,
100–113 in PR #11), the audio subsystem reserves the range **8000–8799**.
A cross-subsystem message ID registry should be formalised in a follow-on
RFC or in `docs/rfcs/README.md` once the Display Server numbering is settled.

| Range | Owner |
|-------|-------|
| 100–199 | Display Server (PR #11) |
| 4000–4032 | Display Server (PR #9) |
| 8000–8299 | Audio Server |
| 8300–8499 | MIDI Server |
| 8500–8799 | Audio Units |

### Relation to Display Server and Verification RFCs

The audio subsystem intersects with three other open PRs:

- **PR #8 (IPC Architecture)**: `ipc_right.c` and `mach_msg.c` are already
  implemented.  `MACH_MSG_OOL_DESCRIPTOR` support (AUDIO-IMP-1) and
  `MACH_MSG_PORT_DESCRIPTOR` (AUDIO-IMP-2) are the remaining IPC work items
  that both the display server and audio server depend on.

- **PRs #9 / #11 (Display Server)**: The Display Server and Audio Server share
  the same four IPC gate conditions (AUDIO-IMP-1 through AUDIO-IMP-4 = IPC-IMP-1
  through IPC-IMP-4).  Implementing them once for the display server unblocks the
  audio server at no additional kernel cost.  The audio subsystem also reuses the
  `com.unhox.*` bootstrap naming convention from PR #9.

- **PR #15 (Kernel Verification / RFC Process)**: The `SCHED_RT` scheduling policy
  required by the audio I/O thread is described at the kernel level in PR #15.
  The four verification layers (L1–L4) defined there apply to the audio subsystem's
  kernel changes: the `SCHED_RT` scheduler and the `ipc_mqueue` priority inheritance
  path are candidates for Layer 3 (TLA+ model) and Layer 4 (Isabelle/HOL proof).
  This RFC adopts the checklist item format and component taxonomy defined in PR #15.

## Alternatives Considered

### Alternative 1: In-kernel audio mixer

Move the audio mixing loop into the kernel to eliminate IPC round-trips.
Rejected because:

- It violates UNHOX's first design principle (kernel minimality).
- XNU collapsed the BSD server into the kernel and introduced severe bugs;
  UNHOX must not repeat this for audio.
- IPC latency on Mach is typically < 20 µs on x86-64; at 128 frames / 48 kHz
  (2.67 ms period) this is well under 1 % of the budget.

### Alternative 2: Wayland-style shared memory ring buffer without IPC

Pass audio data via a raw shared-memory ring buffer (no Mach messages, just
shared `vm_allocate` region + atomic sequence counters).  Used by JACK and
PipeWire on Linux.

Rejected because:

- Requires the client to spin-poll or use a futex-like sleep, which requires
  a new kernel primitive.
- Eliminates the capability model: any task that obtains the region address
  can write arbitrary data into the audio pipeline.
- The Mach OOL descriptor already provides zero-copy at the per-buffer
  granularity; a persistent ring buffer adds implementation complexity for
  marginal latency gain.

### Alternative 3: Load all Audio Units as shared libraries in the Audio Server

All plugins run in-process within the Audio Server for minimum IPC latency.
Used by macOS Core Audio's in-process AU v2 hosting.

Accepted **as an optimisation for trusted system units** (built-in mixer,
SRC, output), rejected as the default model for third-party plugins, because:

- A crash in a third-party plugin kills the Audio Server and silences all audio.
- The out-of-process model allows the Audio Server to continue rendering with
  silence or a bypass on plugin crash, detected via `MACH_NOTIFY_NO_SENDERS`.
- The performance difference is a single IPC round-trip per buffer period per
  plugin; at 128 frames / 48 kHz this is ~2.67 ms / depth-of-graph.

### Alternative 4: Adopt PipeWire or JACK as the audio server

Port PipeWire or JACK to UNHOX instead of implementing a new server.

Rejected because:

- Both are designed for Linux POSIX interfaces (epoll, POSIX shared memory,
  Unix sockets).  Porting requires replacing their entire IPC substrate.
- Neither follows the Mach capability model for access control.
- A purpose-built server that uses Mach IPC natively can be implemented in
  ~2000 lines of C and is far simpler to verify (relevant to PR #15's
  Layer 3/L4 verification goals).

## Open Questions

1. **Plugin sandboxing depth**: Should third-party Audio Unit tasks be placed
   in a restricted task with no `task_self` right (analogous to seL4 untrusted
   task)? Or is port-right-based isolation sufficient?  Decision deferred to
   the security review at Phase 5.

2. **Shared message ID registry**: The Display Server (PRs #9, #11) and Audio
   Server both chose message ID ranges independently.  A central registry
   document (or an extension to `docs/rfcs/README.md`) should canonicalise all
   subsystem message ID ranges before any RFC is marked Accepted.

3. **`SCHED_RT` interface**: Should `thread_set_rt_policy()` be a `mach_msg`
   to the kernel task port, or a new syscall entry in the trap table?  PR #15's
   RFC covers this question; the audio subsystem will follow whatever interface
   that RFC specifies.

4. **USB isochronous scheduling**: USB audio isochronous transfer requires the
   Device Server's USB host controller thread to also run at `SCHED_RT`.  Does
   the `SCHED_RT` policy in PR #15 support multiple RT threads with different
   periods, or only one per CPU?

5. **virtio-sound vs. real HDA for CI**: A virtio-sound QEMU device enables
   full-stack CI tests without real hardware.  Does the virtio-snd driver belong
   in a Phase 3 device driver RFC or can it be included directly in the audio
   subsystem tasks?

6. **AUv3 binary compatibility**: Should a future AUv3 compatibility wrapper
   task be part of the Audio Units framework (`frameworks/AudioUnits/auv3_compat/`)
   or a separate plugin bridge (`servers/audio/auv3_bridge/`)?  AUv3 plugins
   use XPC as transport on macOS; UNHOX would need a `libxpc` shim that maps
   XPC calls to native Mach port messages.

7. **VST3 C++ ABI**: The VST3 bridge (`vst3_bridge.cpp`) requires a C++ compiler
   in the UNHOX build tree.  The kernel and servers are currently pure C.  Should
   the plugin bridges be an optional component built by a separate C++ toolchain,
   or should the VST3 bridge expose only a C wrapper that can be compiled by the
   main Clang C driver?

8. **CLAP vs. VST3 as the primary open plugin format**: CLAP (MIT, newer C ABI)
   is a simpler integration than VST3 (GPL/proprietary dual, C++ COM).  Should the
   first bridge implementation target CLAP rather than VST3?

## Implementation Checklist

All checklist items follow the format defined in `docs/rfcs/README.md` (PR #15).

### Kernel: `SCHED_RT` Scheduling (Phase 2)

- [ ] `[phase:2]` `[component:sched]` Add `SCHED_RT` fields to `struct thread` in `kernel/kern/kern.h`
- [ ] `[phase:2]` `[component:sched]` Implement `SCHED_RT` run queue in `kernel/kern/sched.c`
- [ ] `[phase:2]` `[component:sched]` Add RT preemption to scheduler tick handler
- [ ] `[phase:2]` `[component:sched]` Priority inheritance in `kernel/ipc/ipc_mqueue.c` for RT threads
- [ ] `[phase:2]` `[component:sched]` Add `thread_set_rt_policy()` interface (form per PR #15 decision)
- [ ] `[phase:2]` `[layer:L1]` `[component:sched]` Unit test: SCHED_RT thread preempts SCHED_NORMAL thread

### IPC Gate Conditions (Phase 2, shared with Display Server PRs #9/#11)

- [ ] `[phase:2]` `[component:ipc]` OOL memory descriptor support in `kernel/ipc/ipc_kmsg.c`
- [ ] `[phase:2]` `[component:vm]` COW page remapping for OOL in `kernel/vm/vm_map.c`
- [ ] `[phase:2]` `[component:ipc]` Port right transfer (`MACH_MSG_PORT_DESCRIPTOR`) in `ipc_kmsg.c`
- [ ] `[phase:2]` `[component:ipc]` Blocking receive with timeout in `kernel/ipc/ipc_mqueue.c`
- [ ] `[phase:2]` `[component:bootstrap]` Bootstrap Server `bootstrap_look_up()` RPC
- [ ] `[phase:2]` `[layer:L1]` `[component:ipc]` Test: send 1 MB OOL buffer; verify received unchanged
- [ ] `[phase:2]` `[layer:L1]` `[component:ipc]` Test: port right transfer; client uses transferred right

### Device Server: Audio Hardware Drivers (Phase 3)

- [ ] `[phase:3]` `[component:device]` PCI audio device enumeration (class 0x04)
- [ ] `[phase:3]` `[component:device]` Intel HDA codec driver (BDL, DMA ring buffer, IRQ)
- [ ] `[phase:3]` `[component:device]` USB audio class 2.0 driver (isochronous endpoints)
- [ ] `[phase:3]` `[component:device]` USB MIDI class 1.0 driver (bulk endpoints)
- [ ] `[phase:3]` `[component:device]` virtio-sound driver for QEMU CI testing
- [ ] `[phase:3]` `[layer:L1]` `[component:device]` Integration test: write 440 Hz sine to HDA; no xruns

### MIDI Server (Phase 5)

- [ ] `[phase:5]` `[component:device]` Scaffold `servers/midi/` with `CMakeLists.txt`
- [ ] `[phase:5]` `[component:device]` Implement `midi_server.c` — event loop; register with bootstrap
- [ ] `[phase:5]` `[component:device]` Implement `midi_device.c` — USB MIDI device interface
- [ ] `[phase:5]` `[component:device]` Implement `midi_uart.c` — legacy MPU-401 UART driver
- [ ] `[phase:5]` `[component:device]` Implement `midi_virtual.c` — virtual endpoint management
- [ ] `[phase:5]` `[component:device]` Implement `midi_route.c` — timestamped event routing
- [ ] `[phase:5]` `[layer:L1]` `[component:device]` Unit test: 100 note-on events routed; timestamps preserved
- [ ] `[phase:5]` `[layer:L1]` `[component:device]` Unit test: one source routed to three destinations

### Audio Server (Phase 5)

- [ ] `[phase:5]` `[component:device]` Scaffold `servers/audio/` with `CMakeLists.txt`
- [ ] `[phase:5]` `[component:device]` Implement `audio_server.c` — register with bootstrap; spawn RT thread
- [ ] `[phase:5]` `[component:device]` Implement `audio_device.c` — device enumeration; hot-plug events
- [ ] `[phase:5]` `[component:device]` Implement `audio_session.c` — stream open/close; OOL buffer alloc
- [ ] `[phase:5]` `[component:device]` Implement `audio_format.c` — format negotiation; auto-insert SRC AU
- [ ] `[phase:5]` `[component:device]` Implement `audio_graph.c` — DAG; topological sort; connect/disconnect
- [ ] `[phase:5]` `[component:device]` Implement `audio_rt.c` — SCHED_RT render loop; xrun timeout handling
- [ ] `[phase:5]` `[layer:L1]` `[component:device]` Integration test: 1 s sine output; no xruns under QEMU virtio-sound
- [ ] `[phase:5]` `[layer:L1]` `[component:device]` Benchmark: end-to-end IPC latency ≤ 1 ms at 128 frames/48 kHz

### Audio Units Framework (Phase 5)

- [ ] `[phase:5]` `[component:framework]` Scaffold `frameworks/AudioUnits/` with `CMakeLists.txt`
- [ ] `[phase:5]` `[component:framework]` Write `include/AudioUnit.h` — AU types; render protocol structs
- [ ] `[phase:5]` `[component:framework]` Write `include/AUGraph.h` — client graph builder API
- [ ] `[phase:5]` `[component:framework]` Write `include/MusicDevice.h` — instrument AU MIDI extensions
- [ ] `[phase:5]` `[component:framework]` Implement `AudioUnitBase.c` — render loop; port registration boilerplate
- [ ] `[phase:5]` `[component:framework]` Implement `AUGraph.c` — wraps Audio Server graph IPC
- [ ] `[phase:5]` `[component:framework]` Implement `system/AUMixer.c` — built-in N-channel mixer (in-process)
- [ ] `[phase:5]` `[component:framework]` Implement `system/AUSampleRateConverter.c` — built-in SRC
- [ ] `[phase:5]` `[component:framework]` Implement `system/AUOutput.c` — built-in output AU
- [ ] `[phase:5]` `[layer:L1]` `[component:framework]` Unit test: render 1024-frame sine; THD < −80 dB
- [ ] `[phase:5]` `[layer:L1]` `[component:framework]` Integration test: sine AU → EQ AU → mixer AU → output

### Plugin Format Compatibility Bridges (Phase 5)

- [ ] `[phase:5]` `[component:framework]` Write `servers/audio/lv2_bridge/lv2_bridge.c` — LV2 host wrapper (ISC licence)
- [ ] `[phase:5]` `[component:framework]` Write `servers/audio/vst2_bridge/vst2_bridge.c` — VST2 AEffect wrapper
- [ ] `[phase:5]` `[component:framework]` Write `servers/audio/vst3_bridge/vst3_bridge.cpp` — VST3 IAudioProcessor wrapper (optional C++ build)
- [ ] `[phase:5]` `[component:framework]` Write `servers/audio/clap_bridge/clap_bridge.c` — CLAP host wrapper (MIT licence)
- [ ] `[phase:5]` `[layer:L1]` `[component:framework]` Integration test: LV2 Calf Compressor loaded via bridge; xrun-free render
- [ ] `[phase:5]` `[layer:L1]` `[component:framework]` Integration test: VST2 bridge renders bypass; verify sample-accurate pass-through

### `SCHED_RT` Kernel RFC (Phase 2)

- [ ] `[phase:2]` `[component:docs]` Write `docs/rfcs/RFC-0006-rt-scheduling.md` covering `SCHED_RT` policy API, EDF/CBS algorithms, MCS, and verification plan

### Documentation and CI (Phase 5)

- [x] `[phase:5]` `[component:docs]` Write `docs/rfcs/RFC-0005-audio-subsystem.md` (this document)
- [x] `[phase:5]` `[component:docs]` Write `docs/audio-server-design.md`
- [x] `[phase:5]` `[component:docs]` Update `docs/architecture.md` with audio tier
- [x] `[phase:5]` `[component:docs]` Update `TASKS.md` with Phase 5 audio task checklist
- [ ] `[phase:5]` `[component:docs]` Write `servers/audio/README.md`
- [ ] `[phase:5]` `[component:docs]` Write `servers/midi/README.md`
- [ ] `[phase:5]` `[component:docs]` Write `frameworks/AudioUnits/README.md`
- [ ] `[phase:5]` `[component:ci]` CI job: build audio server + midi server on push
- [ ] `[phase:5]` `[component:ci]` CI job: run audio integration test under QEMU virtio-sound

## References

- Accetta et al., "Mach: A New Kernel Foundation for UNIX Development" (1986)
- Tevanian et al., "Mach Threads and the Unix Kernel: The Battle for Control" (1987)
- OSF MK6 `kern/sched_prim.c` — real-time thread scheduling
- Apple Developer Documentation: Core Audio Overview (2004)
- Apple Developer Documentation: Audio Unit Programming Guide (2012) — AU v2 C API
- Apple Developer Documentation: Audio Unit Extensions (AUv3) for iOS and macOS (2015)
- Apple Core MIDI Framework Reference (2012)
- USB Device Class Definition for MIDI Devices Rev 1.0 (1999, usb.org)
- USB Device Class Definition for Audio Devices Rev 2.0 (2009, usb.org)
- Intel High Definition Audio Specification Rev 1.0a (2010)
- virtio-snd specification, virtio v1.2 §5.14
- Steinberg, "VST Plug-In SDK 2.4" (2006) — VST2 C API and `AEffect` struct
- Steinberg, "VST 3 SDK" (GPL v3 / Steinberg licence) — https://github.com/steinbergmedia/vst3sdk
- Free Eichhorn et al., "CLAP (CLever Audio Plug-in API)" (MIT) — https://github.com/free-audio/clap
- Larsson, "LV2 Core Specification" (ISC) — https://lv2plug.in
- Falkner, "Real-Time Audio in macOS and iOS: AUv3 Out-of-Process Plug-ins" (WWDC 2018, Session 507)
- Levis, "Mixed Criticality Scheduling" in Lipari et al. (eds.) "Embedded Systems Design" (2011)
- UNHOX RFC-0001: IPC Message Format (`docs/rfcs/RFC-0001-ipc-message-format.md`)
- UNHOX PR #8: IPC Architecture implementation (ipc_right.c, mach_msg.c)
- UNHOX PR #9: Display Server Alternatives RFC (IPC-IMP-1 through IPC-IMP-4 gate conditions)
- UNHOX PR #11: Display Server Architecture RFC (message ID allocation precedent)
- UNHOX PR #15: Kernel Functional Correctness RFC (SCHED_RT, verification layers L1–L4)
- UNHOX `docs/audio-server-design.md` (full design detail companion document)
