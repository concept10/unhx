# RFC-0002: Audio Subsystem Architecture

- **Status**: Proposed
- **Author**: UNHOX Project
- **Date**: 2026-03-06

## Summary

This RFC defines how Core Audio-equivalent, Core MIDI-equivalent, and Audio
Unit functionality are structured in UNHOX.  Following strict Mach microkernel
discipline, **no audio logic lives in the kernel**.  Instead, an Audio Server
and a MIDI Server run as ordinary userspace tasks and communicate with clients,
device drivers, and each other exclusively via Mach IPC.  The kernel's only
contribution is real-time thread scheduling and a shared-memory region
mechanism (already provided by the Mach VM subsystem) for low-latency sample
transfer.

## Motivation

Audio on modern systems has three hard requirements that must be addressed at
the architecture level:

1. **Low latency** — a 10 ms hardware buffer requires the audio callback to
   fire with sub-millisecond jitter.  This is a scheduling problem, not an IPC
   problem.  The kernel must provide a real-time scheduling class; everything
   else can stay in userspace.

2. **Extensibility** — audio effects, synthesizers, and format converters must
   be loadable at runtime without kernel rebuilds.  The plugin model (Audio
   Units) is a userspace concern.

3. **Isolation** — a misbehaving audio plugin must not crash the system.
   Running each plugin as a separate task (or in a sandboxed address space)
   and communicating via ports satisfies this requirement.

This RFC documents the design decisions that satisfy all three requirements
inside the UNHOX Mach architecture.

## Design Principles

These follow directly from the five UNHOX design principles in `docs/ai-copilot-unhox.txt`:

- **Kernel minimality**: the kernel adds only a real-time scheduling policy
  (`SCHED_RT`).  All audio graph management, device enumeration, and plugin
  hosting stay in userspace servers.
- **Mach IPC is the API**: every audio operation crosses a Mach port.  There
  is no shared kernel state between audio components.
- **Ports as capabilities**: a send right to an Audio Unit's input port IS
  the authority to write audio data to that unit.  No separate ACL check.
- **Document every decision**: all non-obvious choices in this RFC cite a
  historical precedent or a measured trade-off.

## Kernel Changes Required

### 1. Real-Time Scheduling Class (`SCHED_RT`)

The round-robin scheduler in Phase 1 (`kernel/kern/sched.c`) must be extended
with a real-time policy in Phase 2.  This mirrors the Mach RT scheduling work
described in Tevanian et al. and later formalised in OSF MK.

```
SCHED_NORMAL  — time-sharing (existing round-robin, Phase 1)
SCHED_RT      — fixed priority, preemptive, with a period/computation/
                deadline triple (Mach real-time thread ABI)
```

A thread running at `SCHED_RT` preempts all `SCHED_NORMAL` threads when its
period fires.  The Audio Server uses this policy for its I/O thread.

**Reference**: OSF MK6 `kern/sched_prim.c`; Mach 3.0 Kernel Principles §6.3
(Real-Time Threads).

### 2. Shared-Memory Audio Buffers

High-bandwidth sample data must not be copied on every IPC message.  The Mach
VM `vm_map_copy` / out-of-line (OOL) descriptor mechanism already provides
zero-copy transfer.  No new kernel primitive is needed; the Audio Server
allocates a shared region with `vm_allocate`, then passes an OOL descriptor to
the hardware driver and to each connected Audio Unit.

The IPC message carries only a small control header (buffer pointer, frame
count, timestamp); the sample data itself is referenced by VM mapping.

**Reference**: CMU Mach 3.0 paper §3 (Virtual Memory); RFC-0001 §Complex
Messages.

### 3. High-Precision Timer

Audio callbacks must fire at precise wall-clock intervals.  The kernel already
owns the hardware timer (for the scheduler tick).  The Audio Server registers a
real-time thread whose period matches the hardware buffer duration.  No new
kernel interface is required beyond `SCHED_RT` thread creation.

## Userspace Server Architecture

### Audio Server (`servers/audio/`)

The Audio Server is the single point of contact for all audio I/O in the
system.  It mirrors the role of Core Audio's HAL (Hardware Abstraction Layer)
on macOS.

```
Client Task
    │
    │  mach_msg(audio_render_port, render_msg)
    │
    ▼
Audio Server                  (userspace, SCHED_RT I/O thread)
    │
    ├── Device Server         (hardware driver: HDA, USB audio, virtio-sound)
    ├── Audio Unit Tasks      (effect/instrument/mixer plugins)
    └── MIDI Server           (MIDI event stream → Audio Units)
    │
    ▼
Mach Kernel                   (IPC · VM · SCHED_RT · Tasks · Threads)
```

**Responsibilities:**
- Device enumeration and session management
- Audio graph construction and teardown
- Buffer scheduling and RT callback delivery
- Format conversion and sample-rate negotiation

**IPC Protocol (illustrative):**

```c
/* Client → Audio Server: open a default output stream */
struct audio_open_request {
    mach_msg_header_t  header;
    uint32_t           op;            /* AUDIO_OP_OPEN_OUTPUT */
    uint32_t           sample_rate;   /* e.g. 44100 */
    uint32_t           channel_count; /* e.g. 2 */
    uint32_t           format;        /* AUDIO_FMT_F32_LE */
};

struct audio_open_reply {
    mach_msg_header_t  header;
    kern_return_t      retval;
    mach_port_name_t   stream_port;   /* send right: write audio here */
    mach_port_name_t   buffer_port;   /* OOL shared memory region port */
    uint32_t           buffer_frames; /* frames per hardware period */
};
```

The `stream_port` returned to the client is the send right the client uses to
signal that its buffer slice is filled.  The Audio Server's RT thread wakes,
mixes all connected streams, and pushes the result to the Device Server.

### MIDI Server (`servers/midi/`)

The MIDI Server manages MIDI device I/O and routes MIDI events between sources
and destinations.  It is analogous to Core MIDI on macOS.

```
MIDI Client (synth, DAW, etc.)
    │
    │  mach_msg(midi_port, midi_event_msg)
    │
    ▼
MIDI Server
    │
    ├── Device Server   (USB MIDI class driver, MPU-401 UART driver)
    ├── Virtual MIDI    (software MIDI endpoints for inter-app routing)
    └── Audio Units     (instrument AUs subscribe to MIDI event ports)
```

**Responsibilities:**
- Physical MIDI device enumeration (USB, legacy UART)
- Virtual MIDI endpoint creation
- Timestamped event routing with sub-millisecond precision
- SysEx pass-through

**IPC Protocol (illustrative):**

```c
/* Any source → MIDI Server: send a MIDI event */
struct midi_event_msg {
    mach_msg_header_t  header;
    uint64_t           timestamp;     /* Mach absolute time */
    uint8_t            status;        /* MIDI status byte */
    uint8_t            data1;
    uint8_t            data2;
    uint8_t            _pad;
};
```

MIDI events are small (≤ 4 bytes of payload) and fit entirely inline in the
Mach message header body.  Timestamp is a Mach absolute time value so that the
Audio Server and MIDI Server can synchronize event delivery to the exact sample
frame.

### Audio Units (`frameworks/AudioUnits/`)

Audio Units are the UNHOX equivalent of Apple's Audio Unit plugin standard.
Each Audio Unit is a userspace task (or a shared library loaded into the Audio
Server's address space for trusted system units).

**Unit types:**

| Type | Description | Analogy |
|------|-------------|---------|
| `AU_TYPE_OUTPUT` | Writes to hardware via Audio Server | kAudioUnitType_Output |
| `AU_TYPE_MIXER` | N-input, 1-output mixer | kAudioUnitType_Mixer |
| `AU_TYPE_EFFECT` | In-place DSP (EQ, reverb, …) | kAudioUnitType_Effect |
| `AU_TYPE_INSTRUMENT` | MIDI-driven synthesizer | kAudioUnitType_MusicDevice |
| `AU_TYPE_FORMAT_CONVERTER` | Sample-rate and format conversion | kAudioUnitType_FormatConverter |

**Audio Unit IPC contract:**

Each Audio Unit exposes three ports:
- `au_render_port` — Audio Server calls this each hardware period with an OOL
  buffer descriptor.  The unit fills or transforms the buffer and replies.
- `au_midi_port` — MIDI Server delivers timestamped MIDI events here (for
  instrument and effect units that respond to MIDI).
- `au_control_port` — Host sends parameter-change messages (e.g., filter
  cutoff, gain).

```c
/* Audio Server → Audio Unit: render one buffer period */
struct au_render_request {
    mach_msg_header_t        header;
    uint64_t                 timestamp;       /* host time, sample-accurate */
    uint32_t                 frame_count;
    /* OOL descriptor carrying the buffer VM region follows in body */
};

struct au_render_reply {
    mach_msg_header_t  header;
    kern_return_t      retval;               /* 0 = success */
};
```

**Graph topology:**

The Audio Server maintains the audio graph as a directed acyclic graph (DAG)
of port-connected Audio Units.  The rendering order is computed from the DAG
topology; each node is rendered in dependency order within the RT thread's
period.

```
[Instrument AU] ──► [Effect AU: EQ] ──► [Effect AU: Reverb] ──► [Output AU]
        ▲
[MIDI Server]
```

## Port Naming Convention

Audio subsystem ports are registered with the Bootstrap Server under
well-known names following the pattern `com.unhox.audio.<role>`:

| Bootstrap Name | Description |
|---------------|-------------|
| `com.unhox.audio.server` | Audio Server's public service port |
| `com.unhox.midi.server` | MIDI Server's public service port |
| `com.unhox.audio.default_output` | Default output stream port |
| `com.unhox.audio.default_input` | Default input (microphone) stream port |

## Message ID Ranges

Following MIG convention, the audio subsystem reserves a range of message IDs:

| Range | Subsystem |
|-------|-----------|
| 8000–8099 | Audio Server — device management |
| 8100–8199 | Audio Server — stream management |
| 8200–8299 | Audio Server — graph management |
| 8300–8399 | MIDI Server — device management |
| 8400–8499 | MIDI Server — event routing |
| 8500–8599 | Audio Unit — render protocol |
| 8600–8699 | Audio Unit — control protocol |

## Hard Problems

### 1. Real-Time Deadline Guarantee

The audio I/O thread must wake within ~100 µs of the hardware interrupt.
Mach's priority inversion avoidance (priority inheritance on mutex acquisition)
and the `SCHED_RT` policy together provide this.  The UNHOX implementation
must ensure that no code path in the kernel's IPC fast path acquires a lock
held by a `SCHED_NORMAL` thread when delivering a message to the audio server.

**Mitigation**: IPC lock contention analysis before shipping Phase 5.

### 2. Audio Unit Isolation vs. Latency

Running each Audio Unit as a separate task adds one IPC round-trip per unit
per buffer period.  For a 128-frame buffer at 48 kHz that is ~2.67 ms — tight
but achievable given Mach IPC latency measurements (typically < 20 µs on
x86-64 hardware).

For trusted system units (output mixer, built-in converter), the Audio Server
may load the unit as a shared library in its own address space, bypassing IPC
at the cost of isolation.  This is an explicit, documented trade-off.

**Reference**: XNU in-process Audio Unit hosting (`AudioComponentFindNext`).

### 3. USB Audio Isochronous Transfer

USB isochronous transfer is handled by a USB host controller driver in the
Device Server.  The MIDI Server's USB MIDI class driver and the Audio Server's
USB audio class driver both communicate with the Device Server via Mach IPC.
Scheduling USB frames to meet audio deadlines requires the Device Server's USB
thread to also run at `SCHED_RT` priority.

### 4. Sample-Accurate MIDI Synchronization

MIDI events must be delivered to the instrument Audio Unit at the exact sample
frame they were timestamped for.  The MIDI Server attaches a Mach absolute time
to each event; the Audio Server converts this to a sample offset within the
current buffer period and passes the offset alongside the event in the
`au_midi_port` message.

## File Layout

```
servers/audio/
├── audio_server.c        # Main event loop and RT I/O thread
├── audio_graph.c         # DAG construction, topological sort
├── audio_device.c        # Device Server IPC — open/close hardware streams
├── audio_session.c       # Client session management (open/close streams)
├── audio_format.c        # Format negotiation and conversion dispatch
├── audio_mig.h           # MIG-style message definitions (IDs 8000–8299)
└── README.md

servers/midi/
├── midi_server.c         # Main event loop and USB/UART polling thread
├── midi_device.c         # USB MIDI class driver interface to Device Server
├── midi_route.c          # Event routing: sources → destinations
├── midi_virtual.c        # Virtual MIDI endpoint management
├── midi_mig.h            # MIG-style message definitions (IDs 8300–8499)
└── README.md

frameworks/AudioUnits/
├── include/
│   ├── AudioUnit.h       # AU type definitions and render protocol structs
│   └── MusicDevice.h     # Instrument AU extensions (MIDI note on/off)
├── AudioUnitBase.c       # Boilerplate: port creation, render loop scaffold
├── AudioUnitGraph.c      # Client-side graph construction helpers
└── README.md
```

## Dependencies

| Dependency | Phase | Status |
|------------|-------|--------|
| Mach IPC (ports, send/receive) | Phase 1 | In progress |
| Mach VM OOL descriptors (complex messages) | Phase 2 | Planned |
| `SCHED_RT` real-time scheduling class | Phase 2 | Planned |
| Bootstrap Server (port registration) | Phase 1 | In progress |
| Device Server (HDA/USB audio driver) | Phase 3 | Planned |
| MIDI Server | Phase 5 | This RFC |
| Audio Server | Phase 5 | This RFC |
| Audio Units framework | Phase 5 | This RFC |

## Implementation Order

1. **Kernel** — add `SCHED_RT` policy to `kernel/kern/sched.c` (Phase 2)
2. **Kernel** — verify OOL descriptor support in `kernel/ipc/ipc_kmsg.c` (Phase 2)
3. **Device Server** — add HDA and USB audio class drivers (Phase 3)
4. **MIDI Server** — USB MIDI enumeration and virtual endpoints (Phase 5)
5. **Audio Server** — RT I/O thread, session management, device interface (Phase 5)
6. **Audio Units framework** — base class, render protocol, graph helpers (Phase 5)
7. **System Audio Units** — output mixer, sample-rate converter, EQ (Phase 5)
8. **Integration test** — sine-wave generator AU → mixer → output (Phase 5)

## References

- Accetta et al., "Mach: A New Kernel Foundation for UNIX Development" (1986)
- Tevanian et al., "Mach Threads and the Unix Kernel: The Battle for Control" (1987)
- OSF MK6 `kern/sched_prim.c` — real-time thread scheduling
- Apple Developer Documentation: Core Audio Overview (2004)
- Apple Developer Documentation: Audio Unit Programming Guide (2012)
- Apple Developer Documentation: Core MIDI Framework Reference
- USB Device Class Definition for MIDI Devices, Revision 1.0 (1999)
- USB Device Class Definition for Audio Devices, Revision 2.0 (2009)
- UNHOX RFC-0001: IPC Message Format
- UNHOX `docs/bsd-server-design.md`
