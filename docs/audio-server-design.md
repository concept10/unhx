# Audio Server Architecture

Design document for the UNHOX Audio Server, MIDI Server, and Audio Units
framework — the components that provide Core Audio-equivalent functionality
on top of the Mach microkernel.

## Overview

In a Mach microkernel system the kernel provides only IPC, VM, tasks, and
threads.  All audio functionality — device I/O, mixing, effects processing,
MIDI routing — is implemented by **userspace servers** and **framework tasks**
that communicate exclusively via Mach ports.

UNHOX follows the same split as NeXTSTEP / macOS Core Audio, but reimplemented
in pure Mach IPC without any in-kernel shortcuts:

```
  Application (DAW, media player, game, …)
       │
       │  Mach IPC: stream open/close, render notify
       │
       ▼
  Audio Server              (userspace, SCHED_RT I/O thread)
       │
       ├── MIDI Server       (MIDI event routing, USB/virtual devices)
       ├── Audio Unit tasks  (effects, instruments, converters — each a task)
       └── Device Server     (hardware: HDA codec, USB audio, virtio-sound)
       │
       ▼
  Mach Kernel               (IPC · VM · SCHED_RT · Tasks · Threads)
```

The kernel gains exactly one new primitive to support audio: a real-time
scheduling policy (`SCHED_RT`).  Everything else reuses existing Mach
mechanisms.

---

## Component Responsibilities

### Audio Server (`servers/audio/`)

The Audio Server is the HAL (Hardware Abstraction Layer) for UNHOX audio.
It owns:

- **Device management**: enumerating audio hardware by querying the Device
  Server; presenting logical audio devices to clients.
- **Session management**: allocating per-client stream ports, negotiating
  sample rate and format.
- **Audio graph**: maintaining a DAG of connected Audio Units; computing
  topological render order.
- **RT I/O thread**: a `SCHED_RT` thread that fires every hardware buffer
  period, renders the audio graph, and pushes the result to the Device Server.
- **Format conversion dispatch**: routing format-mismatch streams through a
  converter Audio Unit automatically.

### MIDI Server (`servers/midi/`)

The MIDI Server manages all MIDI I/O in the system:

- **Physical devices**: USB MIDI class driver (via Device Server), legacy
  MPU-401 UART driver.
- **Virtual endpoints**: software MIDI sources and destinations for inter-task
  MIDI routing.
- **Timestamped event delivery**: every event carries a Mach absolute time
  that the Audio Server converts to a sample-accurate offset.
- **SysEx pass-through**: large SysEx messages use OOL descriptors.

### Audio Units (`frameworks/AudioUnits/`)

Audio Units are the UNHOX plugin model.  Each unit is a userspace task
(untrusted third-party plugins) or a shared library loaded into the Audio
Server's address space (trusted system units for lower latency).

| Unit Type | Role |
|-----------|------|
| `AU_TYPE_OUTPUT` | Writes the final mix to hardware via the Audio Server |
| `AU_TYPE_MIXER` | Sums N input streams into one output |
| `AU_TYPE_EFFECT` | In-place DSP: EQ, dynamics, reverb, delay |
| `AU_TYPE_INSTRUMENT` | MIDI-driven synthesizer (responds to `au_midi_port`) |
| `AU_TYPE_FORMAT_CONVERTER` | Sample-rate and channel-count conversion |

---

## Kernel Prerequisites

### `SCHED_RT` — Real-Time Scheduling Policy

The existing `SCHED_NORMAL` (round-robin) policy in `kernel/kern/sched.c` must
be extended with a real-time policy that accepts a `(period, computation,
deadline)` triple, following the OSF MK6 model.

```c
/* New fields in struct thread (kernel/kern/kern.h) */
sched_policy_t  th_sched_policy;   /* SCHED_NORMAL | SCHED_RT */
uint64_t        th_rt_period;      /* period in Mach time units */
uint64_t        th_rt_computation; /* worst-case CPU per period */
uint64_t        th_rt_deadline;    /* deadline relative to period start */
```

An `SCHED_RT` thread preempts all `SCHED_NORMAL` threads the moment its period
fires and it is runnable.  Priority inversion is avoided by standard Mach
priority inheritance on port receive.

**Files to modify:**
- `kernel/kern/sched.c` — add RT run queue and preemption logic
- `kernel/kern/kern.h` — add RT fields to `thread` struct
- `kernel/kern/thread.c` — honour RT policy on `thread_create`
- `kernel/ipc/ipc_mqueue.c` — priority-inherit when waking a waiting thread

### Shared-Memory Audio Buffers (Mach OOL)

No new kernel primitive is needed.  The Audio Server uses complex Mach messages
with OOL memory descriptors (RFC-0001 §Complex Messages) to hand zero-copy
buffer regions to Audio Unit tasks:

```c
/* Audio Server → Audio Unit: render request with OOL buffer */
struct au_render_request {
    mach_msg_header_t          header;
    mach_msg_body_t            body;
    mach_msg_ool_descriptor_t  buffer;   /* VM region containing samples */
    /* inline payload follows */
    uint64_t  timestamp;    /* Mach absolute time */
    uint32_t  frame_count;
    uint32_t  channel_count;
    uint32_t  format;       /* AUDIO_FMT_F32_LE, etc. */
};
```

The OOL descriptor maps the same physical pages into both the Audio Server's
and the Audio Unit task's address spaces with no data copy.

---

## IPC Design

### Port Topology

```
Bootstrap Server
   ├── "com.unhox.audio.server"  → Audio Server service port
   └── "com.unhox.midi.server"   → MIDI Server service port

Per client session (after AUDIO_OP_OPEN_OUTPUT):
   ├── stream_port   → client writes render-complete notifications here
   ├── buffer_port   → OOL shared ring-buffer region
   └── control_port  → client sends volume/mute changes here

Per Audio Unit instance:
   ├── au_render_port  → Audio Server sends render requests here
   ├── au_midi_port    → MIDI Server delivers timestamped events here
   └── au_control_port → Host sends parameter changes here
```

### Message ID Assignments

| Range | Owner | Operations |
|-------|-------|------------|
| 8000–8099 | Audio Server | Device list, default device get/set |
| 8100–8199 | Audio Server | Stream open, close, format, volume |
| 8200–8299 | Audio Server | Graph add/remove node, connect/disconnect |
| 8300–8399 | MIDI Server | Device list, virtual endpoint create/destroy |
| 8400–8499 | MIDI Server | Event routing: add/remove route |
| 8500–8599 | Audio Unit | Render request/reply |
| 8600–8699 | Audio Unit | MIDI event deliver |
| 8700–8799 | Audio Unit | Control parameter get/set |

### Audio Server Protocol — Key Messages

```c
/* ─── Device Management (8000–8099) ─── */

/* 8001: list all audio devices */
struct audio_list_devices_reply {
    mach_msg_header_t  header;
    kern_return_t      retval;
    uint32_t           count;
    struct {
        uint32_t  device_id;
        uint32_t  max_outputs;
        uint32_t  max_inputs;
        char      name[64];
    } devices[];   /* inline array */
};

/* ─── Stream Management (8100–8199) ─── */

/* 8101: open an output stream */
struct audio_open_output_request {
    mach_msg_header_t  header;
    uint32_t           op;            /* 8101 */
    uint32_t           device_id;     /* 0 = default */
    uint32_t           sample_rate;   /* e.g. 48000 */
    uint32_t           channel_count; /* e.g. 2 */
    uint32_t           format;        /* AUDIO_FMT_F32_LE */
    uint32_t           buffer_frames; /* requested; server may adjust */
};

struct audio_open_output_reply {
    mach_msg_header_t  header;
    kern_return_t      retval;
    mach_port_name_t   stream_port;   /* client writes render-done here */
    mach_port_name_t   buffer_port;   /* OOL ring buffer */
    uint32_t           actual_frames; /* server-chosen buffer size */
};

/* 8102: close a stream */
struct audio_close_request {
    mach_msg_header_t  header;
    uint32_t           op;            /* 8102 */
    mach_port_name_t   stream_port;
};
```

### MIDI Server Protocol — Key Messages

```c
/* ─── Event Routing (8400–8499) ─── */

/* 8401: send a MIDI event to a destination port */
struct midi_event_msg {
    mach_msg_header_t  header;
    uint64_t           timestamp;   /* Mach absolute time */
    uint8_t            status;      /* MIDI status byte (channel + type) */
    uint8_t            data1;       /* note number, CC number, etc. */
    uint8_t            data2;       /* velocity, CC value, etc. */
    uint8_t            _pad;
};

/* 8402: send a SysEx message (variable length, uses OOL) */
struct midi_sysex_msg {
    mach_msg_header_t          header;
    mach_msg_body_t            body;
    mach_msg_ool_descriptor_t  sysex_data;
    uint64_t                   timestamp;
    uint32_t                   length;
};
```

### Audio Unit Render Protocol

```c
/* Audio Server → AU: render one buffer period (msg ID 8501) */
struct au_render_request {
    mach_msg_header_t          header;
    mach_msg_body_t            body;
    mach_msg_ool_descriptor_t  buffer;        /* shared sample buffer */
    uint64_t                   timestamp;     /* host time, sample-accurate */
    uint32_t                   frame_count;
    uint32_t                   channel_count;
    uint32_t                   format;        /* AUDIO_FMT_F32_LE */
    uint32_t                   action;        /* AU_RENDER | AU_BYPASS */
};

struct au_render_reply {
    mach_msg_header_t  header;
    kern_return_t      retval;   /* KERN_SUCCESS or KERN_ABORTED */
};

/* MIDI Server → instrument AU: note-on/off and CC (msg ID 8601) */
struct au_midi_event_msg {
    mach_msg_header_t  header;
    uint64_t           sample_offset;  /* frames from period start */
    uint8_t            status;
    uint8_t            data1;
    uint8_t            data2;
    uint8_t            _pad;
};

/* Host → AU: set a named parameter (msg ID 8701) */
struct au_set_param_msg {
    mach_msg_header_t  header;
    uint32_t           param_id;
    float              value;
    uint32_t           scope;    /* AU_SCOPE_GLOBAL | AU_SCOPE_INPUT | … */
    uint32_t           element;  /* bus / channel index */
};
```

---

## Audio Graph

The Audio Server maintains the rendering graph as a DAG.  Nodes are Audio Unit
instance records; edges are port connections.  Each edge carries the agreed
sample format so mismatches are detected at connection time.

```
                ┌────────────────────────────────────────────┐
                │             Audio Graph (Audio Server)     │
                │                                            │
  ┌──────────┐  │  ┌──────────┐   ┌───────┐   ┌──────────┐  │
  │ Synth AU │──┼─►│  EQ AU   │──►│Reverb │──►│ Mixer AU │──┼──► hardware
  └──────────┘  │  └──────────┘   │  AU   │   └──────────┘  │
  ┌──────────┐  │                 └───────┘        ▲         │
  │ File AU  │──┼─────────────────────────────────►│         │
  └──────────┘  │                                            │
                └────────────────────────────────────────────┘
```

**Render order** is computed by topological sort of the DAG.  The Audio
Server's RT thread calls each AU's `au_render_port` in dependency order within
one hardware period.

**Graph operations** (message IDs 8200–8299):

| ID | Operation |
|----|-----------|
| 8201 | `graph_add_node` — instantiate an AU task |
| 8202 | `graph_remove_node` — terminate an AU task |
| 8203 | `graph_connect` — connect output bus of one AU to input of another |
| 8204 | `graph_disconnect` — remove a connection |
| 8205 | `graph_start` — begin RT rendering |
| 8206 | `graph_stop` — halt RT rendering |

---

## Hard Problems

### 1. Render Deadline Overruns

If an Audio Unit task does not reply to `au_render_request` before the
hardware deadline, the Audio Server must either:
- Insert silence for the overrunning AU (fail-safe), or
- Bypass the AU for this period and log a xrun counter.

The RT thread must never block indefinitely.  All `mach_msg` receive calls in
the RT loop use a finite timeout (equal to the hardware period minus a safety
margin).

### 2. Priority Inheritance Through the Audio Graph

A render chain of depth N requires N nested IPC round-trips.  Each hop must
inherit the `SCHED_RT` priority so no intermediate task is preempted.  Mach's
priority-inheritance protocol on port receive handles this automatically,
provided the AU tasks have registered their receive thread priority correctly
at setup time.

### 3. Hot-Plug Device Changes

When a USB audio device is inserted or removed, the Device Server notifies the
Audio Server via a Mach notification message.  The Audio Server must:
1. Re-enumerate devices
2. Migrate active streams to a fallback device (or notify clients of the
   disconnect via their control ports)
3. Restart the RT I/O thread with the new hardware period

This is an asynchronous operation and must not stall the currently running RT
thread.  A separate management thread in the Audio Server handles hot-plug
events.

### 4. Sample-Rate Conversion

If a client requests 44100 Hz but the hardware runs at 48000 Hz, the Audio
Server automatically inserts a `AU_TYPE_FORMAT_CONVERTER` node into the graph.
The converter AU is a trusted system unit loaded in-process to avoid the extra
IPC hop on every buffer period.

### 5. Audio Unit Crash Isolation

If an out-of-process Audio Unit task crashes, its `au_render_port` receive
right is destroyed.  The Audio Server detects this via a `MACH_NOTIFY_NO_SENDERS`
dead-name notification and removes the crashed AU from the graph, replacing it
with a silence generator for that bus.

---

## Plugin Format Compatibility Bridges

UNHOX Audio Units are structurally equivalent to Apple AUv3 out-of-process
plug-ins: every third-party plugin runs in an isolated Mach task, the same
isolation guarantee that AUv3 provides via XPC process sandboxing.  Each
bridge task is an UNHOX AU that also speaks a foreign plugin SDK internally:

```
┌──────────────────────────────────────────────────────────────────┐
│  Plugin Bridge Task                                              │
│  ┌────────────────────────┐    ┌──────────────────────────────┐  │
│  │  Native SDK side       │    │  UNHOX AU side               │  │
│  │  (VST2/VST3/LV2/CLAP) │◄──►│  au_render_port (IPC 8501)  │  │
│  │  dlopen(plugin.so)    │    │  au_midi_port   (IPC 8601)  │  │
│  │  process(buffer)      │    │  au_control_port(IPC 8701)  │  │
│  └────────────────────────┘    └──────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### LV2 Bridge (Phase 5 — open source, ISC licence)

LV2 plugins expose a C ABI (`LV2_Descriptor`) with atom event ports for
MIDI.  The bridge initialises the plugin via `lv2_descriptor()->instantiate()`,
maps the UNHOX OOL buffer as the audio port, and calls `run()` each render
period.  Atom event packets translate directly to `au_midi_event_msg`.

File: `servers/audio/lv2_bridge/lv2_bridge.c`

### VST2 Bridge (Phase 5 — Steinberg Free SDK)

VST2 plugins expose an `AEffect` struct with `processReplacing()`.  The bridge
calls `VSTPluginMain()`, stores the returned `AEffect *`, and wraps the render
callback.  Parameter changes (`effSetParam`) map to `au_set_param_msg`.

File: `servers/audio/vst2_bridge/vst2_bridge.c`

### VST3 Bridge (Phase 5 — optional C++ build)

VST3 uses a COM-like C++ interface (`IComponent`, `IAudioProcessor`).  The
bridge links against the open-source Steinberg VST3 SDK (GPL v3 / dual).
Because the SDK requires C++, the bridge is an **optional component** enabled
by `UNHOX_ENABLE_VST3_BRIDGE` in CMake and compiled with `clang++`.

File: `servers/audio/vst3_bridge/vst3_bridge.cpp`

### CLAP Bridge (Phase 5 — MIT licence)

CLAP (CLever Audio Plug-in API) has a clean C ABI similar to LV2 but with
explicit parameter automation, thread-safety annotations, and a process-isolation
model that maps directly to UNHOX tasks.  CLAP is the recommended starting point
for new UNHOX-native plugin development.

File: `servers/audio/clap_bridge/clap_bridge.c`

### AAX Bridge (Architecture only — out-of-tree)

AAX (Avid) requires a signed NDA SDK.  The bridge architecture is identical to
the others but cannot be shipped in this repository.  See
`docs/rfcs/RFC-0005-audio-subsystem.md §Plugin Format Compatibility Bridges`.

---

## File Layout

```
servers/audio/
├── audio_server.c        # main(): register with bootstrap, spawn threads
├── audio_device.c        # query Device Server; logical device table
├── audio_session.c       # per-client session: open/close stream ports
├── audio_graph.c         # DAG of AU instances; topological sort
├── audio_rt.c            # SCHED_RT I/O thread: render loop, xrun handling
├── audio_format.c        # format negotiation; auto-insert SRC node
├── audio_mig.h           # MIG message struct definitions (IDs 8000–8299)
├── lv2_bridge/
│   └── lv2_bridge.c      # LV2 host wrapper (ISC, Phase 5)
├── vst2_bridge/
│   └── vst2_bridge.c     # VST2 AEffect wrapper (Steinberg Free, Phase 5)
├── vst3_bridge/
│   └── vst3_bridge.cpp   # VST3 IAudioProcessor wrapper (optional C++, Phase 5)
├── clap_bridge/
│   └── clap_bridge.c     # CLAP host wrapper (MIT, Phase 5)
└── README.md

servers/midi/
├── midi_server.c         # main(): register with bootstrap, poll USB/UART
├── midi_device.c         # USB MIDI class driver via Device Server
├── midi_uart.c           # legacy MPU-401 UART driver
├── midi_route.c          # event routing: source port → destination port(s)
├── midi_virtual.c        # virtual MIDI endpoint create/destroy
├── midi_mig.h            # MIG message struct definitions (IDs 8300–8499)
└── README.md

frameworks/AudioUnits/
├── include/
│   ├── AudioUnit.h           # AU type constants, render protocol structs
│   ├── AUGraph.h             # client-side graph construction API
│   └── MusicDevice.h         # instrument AU extensions (MIDI)
├── AudioUnitBase.c           # boilerplate: port registration, render loop
├── AUGraph.c                 # client-side graph builder (wraps Audio Server IPC)
├── system/
│   ├── AUMixer.c             # built-in N-input mixer AU (in-process)
│   ├── AUSampleRateConverter.c  # built-in SRC AU (in-process)
│   └── AUOutput.c            # built-in output AU (writes to Audio Server)
└── README.md
```

---

## Dependencies

| Dependency | Phase | Notes |
|------------|-------|-------|
| Mach IPC — ports, send/receive | Phase 1 | In progress |
| Mach VM OOL descriptors | Phase 2 | Required for zero-copy audio buffers |
| `SCHED_RT` scheduling policy | Phase 2 | Required for audio RT thread |
| Bootstrap Server | Phase 1 | Port name registration |
| Device Server — PCI/USB enumeration | Phase 3 | Required for hardware audio |
| Device Server — HDA codec driver | Phase 3 | Intel HDA (QEMU default) |
| Device Server — USB audio class driver | Phase 3 | For USB interfaces |
| Device Server — USB MIDI class driver | Phase 3 | For USB MIDI controllers |

---

## Implementation Order

The audio subsystem spans Phase 2 (kernel RT scheduling) and Phase 5 (audio
servers):

### Phase 2 additions (kernel)

1. Add `SCHED_RT` policy fields to `struct thread` in `kernel/kern/kern.h`
2. Implement RT run queue in `kernel/kern/sched.c`
3. Add RT preemption to the scheduler tick handler
4. Wire up priority inheritance in `kernel/ipc/ipc_mqueue.c`
5. Add `thread_set_rt_policy()` kernel call and test from a userspace task

### Phase 3 additions (device drivers)

6. HDA codec driver in `servers/device/` — expose audio I/O ports to Audio Server
7. USB audio class 2.0 driver in `servers/device/` — isochronous endpoints
8. USB MIDI class driver in `servers/device/` — bulk endpoints

### Phase 5 — Audio and MIDI servers

9. Scaffold `servers/audio/` and `servers/midi/` with CMakeLists.txt
10. Implement MIDI Server: device enumeration, virtual endpoints, event routing
11. Implement Audio Server: device management, session management, format negotiation
12. Implement Audio Server RT render loop (`audio_rt.c`)
13. Implement Audio Graph (`audio_graph.c`): DAG, topological sort, connect/disconnect
14. Build system Audio Units: mixer, SRC, output
15. Implement `frameworks/AudioUnits/` client library: `AUGraph.c`, `AudioUnitBase.c`
16. Implement LV2 bridge (`servers/audio/lv2_bridge/`) — first plugin format bridge
17. Implement VST2 bridge (`servers/audio/vst2_bridge/`) — frozen Steinberg C ABI
18. Implement CLAP bridge (`servers/audio/clap_bridge/`) — MIT, modern C ABI
19. Integration test: sine-wave instrument AU → EQ effect AU → mixer AU → hardware output
20. Benchmark: measure round-trip latency at 128-frame / 48 kHz buffer size
21. Document xrun handling and RT deadline behaviour in `docs/rfcs/`

---

## References

- Accetta et al., "Mach: A New Kernel Foundation for UNIX Development" (1986)
- Tevanian et al., "Mach Threads and the Unix Kernel: The Battle for Control" (1987)
- OSF MK6 `kern/sched_prim.c` — real-time thread scheduling
- Apple Developer Documentation: Core Audio Overview (2004)
- Apple Developer Documentation: Audio Unit Programming Guide (2012) — AU v2 C API
- Apple Developer Documentation: Audio Unit Extensions (AUv3) for iOS and macOS (2015)
- Apple Core MIDI Framework Reference (2012)
- Falkner, "Real-Time Audio in macOS and iOS: AUv3 Out-of-Process Plug-ins" (WWDC 2018, Session 507)
- Steinberg, "VST Plug-In SDK 2.4" (2006) — VST2 C API and `AEffect` struct
- Steinberg, "VST 3 SDK" (GPL v3 / Steinberg licence) — https://github.com/steinbergmedia/vst3sdk
- Free Eichhorn et al., "CLAP (CLever Audio Plug-in API)" (MIT) — https://github.com/free-audio/clap
- Larsson, "LV2 Core Specification" (ISC) — https://lv2plug.in
- USB Device Class Definition for MIDI Devices Rev 1.0 (1999, usb.org)
- USB Device Class Definition for Audio Devices Rev 2.0 (2009, usb.org)
- Intel High Definition Audio Specification Rev 1.0a (2010)
- UNHOX RFC-0001: IPC Message Format (`docs/rfcs/RFC-0001-ipc-message-format.md`)
- UNHOX RFC-0005: Audio Subsystem Architecture (`docs/rfcs/RFC-0005-audio-subsystem.md`)
- UNHOX BSD Server Design (`docs/bsd-server-design.md`)
