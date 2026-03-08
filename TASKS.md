# UNHOX Task List

Actionable tasks for the UNHOX project, organized by phase.
Check off items as they are completed.

---

## Phase 0 — Source Archaeology & Repository Setup

### Repository Structure ✅
- [x] Create `kernel/` with `ipc/`, `vm/`, `kern/`, `platform/` subdirectories
- [x] Create `servers/` with `bsd/`, `vfs/`, `device/`, `network/`, `auth/` subdirectories
- [x] Create `frameworks/` with `DisplayServer/` directory
- [x] Create `archive/` with `cmu-mach/`, `osf-mk/`, `utah-oskit/`, `next-docs/` subdirectories
- [x] Create `docs/`, `tools/`, `tests/` directories
- [x] Write README files for all directories

### Git Submodules — Framework Layer ✅
- [x] Add `gnustep/libobjc2` as submodule at `frameworks/objc-runtime/`
- [x] Add `gnustep/libs-base` as submodule at `frameworks/Foundation/`
- [x] Add `gnustep/libs-gui` as submodule at `frameworks/AppKit/`
- [x] Add `apple/swift-corelibs-libdispatch` as submodule at `frameworks/libdispatch/`
- [x] Add `apple/swift-corelibs-foundation` as submodule at `frameworks/CoreFoundation/`

### Source Archaeology — Kernel References
- [ ] Mirror / obtain CMU Mach 3.0 source tree → `archive/cmu-mach/`
  - Source: http://www.cs.utah.edu/flux/mach4/ or bitsavers
  - Key files: `kernel/`, `include/mach/`, `bootstrap/`
  - Run: `./tools/mirror-archives.sh --cmu`
- [ ] Mirror / obtain OSF MK6 or MK7 source → `archive/osf-mk/`
  - Source: MkLinux archives, OSF/RI mirrors
  - Run: `./tools/mirror-archives.sh --osf`
- [ ] Mirror Utah OSKit + Lites → `archive/utah-oskit/`
  - Source: http://www.cs.utah.edu/flux/oskit/ and flux/lites
  - Run: `./tools/mirror-archives.sh --utah`
- [ ] Archive NeXTSTEP/OPENSTEP documentation → `archive/next-docs/`
  - Source: https://bitsavers.org/pdf/next/ and archive.org
  - Run: `./tools/mirror-archives.sh --next`

### Build System ✅
- [x] Create `CMakeLists.txt` at root for cross-compilation setup
- [x] Create `tools/cross/` with cross-compiler toolchain instructions
  - Target: `x86_64-unknown-elf` via Clang/LLD (see `cmake/x86_64-elf-clang.cmake`)
- [x] Create `tools/qemu-run.sh` script for QEMU boot testing
- [x] Create Nix flake (`flake.nix`) for reproducible dev environment
- [x] Document build prerequisites in `docs/build-setup.md`

### Documentation ✅
- [x] Write `docs/architecture.md` — system architecture overview
- [x] Write `docs/roadmap.md` — development roadmap with milestones
- [x] Write `docs/sources.md` — complete source inventory with licenses
- [x] Write `docs/ipc-design.md` — IPC subsystem design decisions
- [x] Write `docs/bsd-server-design.md` — BSD server architecture and hard problems
- [x] Create `docs/rfcs/` directory and write first RFC (RFC-0001: IPC message format)

---

## Phase 1 — Kernel Core

### x86-64 Platform Bring-up
- [ ] Write `kernel/platform/x86_64/boot.S` — Multiboot2 entry point
- [ ] Write `kernel/platform/x86_64/gdt.c` — Global Descriptor Table setup
- [ ] Write `kernel/platform/x86_64/idt.c` — Interrupt Descriptor Table
- [ ] Write `kernel/platform/x86_64/pmap.c` — 4-level page table management
- [ ] Write `kernel/platform/x86_64/uart.c` — 16550 serial console driver
- [ ] Verify: kernel boots under QEMU and prints "UNHOX" via serial
- [ ] Write `tests/integration/boot/boot_test.sh` — QEMU boot smoke test

### Physical Memory
- [ ] Write `kernel/vm/vm_page.c` — physical page frame allocator
- [ ] Parse Multiboot memory map to initialize page allocator
- [ ] Write unit test: allocate and free physical pages

### Kernel Heap
- [ ] Write `kernel/kern/kmem.c` — kernel heap allocator (simple slab or buddy)
- [ ] Write unit test: allocate and free kernel objects

### Tasks and Threads
- [ ] Write `kernel/kern/task.c` — task create, terminate, reference counting
- [ ] Write `kernel/kern/thread.c` — thread create, context save/restore
- [ ] Write `kernel/platform/x86_64/context.S` — context switch assembly
- [ ] Write `kernel/kern/sched.c` — basic round-robin scheduler
- [ ] Verify: two threads run concurrently and print alternating output

### Virtual Memory
- [ ] Write `kernel/vm/vm_map.c` — per-task address space management
- [ ] Write `kernel/vm/vm_object.c` — backing memory object lifecycle
- [ ] Write `kernel/vm/vm_fault.c` — page fault handler
- [ ] Verify: user task runs in its own address space

### IPC
- [ ] Write `kernel/ipc/ipc_port.c` — Mach port creation and lifecycle
- [ ] Write `kernel/ipc/ipc_space.c` — per-task port name space
- [ ] Write `kernel/ipc/ipc_right.c` — port right management
- [ ] Write `kernel/ipc/ipc_mqueue.c` — message queue with blocking
- [ ] Write `kernel/ipc/ipc_kmsg.c` — kernel message allocation
- [ ] Write `kernel/ipc/mach_msg.c` — `mach_msg()` trap
- [ ] Write `tests/ipc/ipc_roundtrip_test.c` — two-task message-passing test
- [ ] Verify milestone v0.2: two tasks pass a Mach message

### Bootstrap Server
- [ ] Write `kernel/kern/bootstrap.c` — initial service registration
- [ ] Verify milestone v0.3: bootstrap server registers device and BSD servers

### IPC Performance Baseline
- [ ] Write `tests/ipc/ipc_perf.c` — null Mach message round-trip benchmark
- [ ] Run benchmark under QEMU and record baseline
- [ ] Run benchmark on bare metal (when available) and record
- [ ] Document results in `docs/research/ipc-performance.md`

---

## Phase 2 — System Servers

### BSD Server
- [ ] Design BSD server IPC protocol (MIG or hand-written)
- [ ] Implement `fork()` — clone task + address space
- [ ] Implement `exec()` — load ELF binary into new address space
- [ ] Implement `exit()` / `wait()` — process lifecycle
- [ ] Implement signal delivery across task boundary
- [ ] Implement file descriptor table (backed by VFS server)
- [ ] Verify milestone v0.4: fork + exec + basic syscalls work

### VFS Server (ramfs)
- [ ] Implement ramfs — in-memory directory tree
- [ ] Implement `open()`, `read()`, `write()`, `close()`, `stat()`
- [ ] Implement `readdir()`, `mkdir()`, `unlink()`
- [ ] Verify: BSD server can open and read `/bin/sh` from ramfs

### Shell
- [ ] Port `dash` (Debian Almquist Shell) to UNHOX
- [ ] Verify milestone v0.5: shell prompt appears

---

## Phase 3 — Driver Layer

### QEMU Virtio Drivers (development priority)
- [ ] Implement virtio-blk block device driver
- [ ] Implement virtio-net network device driver
- [ ] Verify: read/write to virtio-blk disk image

### Storage & Filesystem
- [ ] Implement AHCI SATA driver in device server
- [ ] Implement NVMe driver in device server
- [ ] Implement ext2 filesystem translator in VFS server
- [ ] Verify milestone v0.6: boot from disk, read/write files

### Input & Display
- [ ] Implement USB HID keyboard driver
- [ ] Implement VESA/GOP framebuffer driver
- [ ] Implement PCI enumeration

---

## Phase 4 — Framework Layer

### Objective-C Runtime
- [ ] Build `frameworks/objc-runtime/` (libobjc2 submodule) for UNHOX userspace
- [ ] Resolve any POSIX threading dependencies
- [ ] Verify: simple Objective-C program compiles and runs on UNHOX

### GNUstep Foundation
- [ ] Build `frameworks/Foundation/` (libs-base submodule) for UNHOX
- [ ] Port NSThread to use Mach thread primitives directly
- [ ] Port NSRunLoop to use Mach port notification
- [ ] Verify milestone v0.7: GNUstep Foundation app runs on UNHOX

### libdispatch (GCD)
- [ ] Build libdispatch with Mach port integration
- [ ] Verify: `dispatch_async` works on UNHOX

---

## Phase 5 — Audio Subsystem

Design reference: `docs/rfcs/RFC-0005-audio-subsystem.md` and
`docs/audio-server-design.md`.

### Kernel — Real-Time Scheduling (`SCHED_RT`)

These are the only kernel changes the audio subsystem requires.

- [ ] Add `sched_policy_t`, `th_rt_period`, `th_rt_computation`,
      `th_rt_deadline` fields to `struct thread` in `kernel/kern/kern.h`
- [ ] Implement `SCHED_RT` run queue in `kernel/kern/sched.c`
  - Fixed-priority, preemptive; fires when thread period elapses
  - Preempts all `SCHED_NORMAL` threads immediately
- [ ] Add RT preemption to the scheduler tick handler (platform timer interrupt)
- [ ] Add priority inheritance in `kernel/ipc/ipc_mqueue.c` — when an
      `SCHED_RT` thread blocks on a port receive, propagate its priority to
      the port's owner thread
- [ ] Add `thread_set_rt_policy(thread, period, computation, deadline)` kernel
      call, accessible via `mach_msg` to the kernel task port
- [ ] Write unit test: create two threads at SCHED_RT and SCHED_NORMAL;
      verify RT thread runs first and meets its deadline
- [ ] Reference: OSF MK6 `kern/sched_prim.c`; Mach 3.0 Kernel Principles §6.3

### Device Server — Audio Hardware Drivers

- [ ] Enumerate PCI audio devices in `servers/device/` using PCI class 0x04
      (Multimedia Controller)
- [ ] Implement Intel HDA (High Definition Audio) codec driver
  - Stream descriptor setup (BDL — buffer descriptor list)
  - DMA ring buffer for output and input
  - IRQ handler via Device Server interrupt port
  - Expose logical audio I/O ports to the Audio Server
  - Reference: Intel HDA Specification Rev 1.0a
- [ ] Implement USB audio class 2.0 driver
  - Isochronous endpoint scheduling via USB host controller
  - Sample-rate negotiation via UAC2 control interface
  - Hot-plug/unplug notification to Audio Server
  - Reference: USB Audio Class 2.0 specification (usb.org)
- [ ] Implement USB MIDI class driver
  - Bulk endpoint polling for MIDI IN; bulk endpoint writes for MIDI OUT
  - Expose MIDI source and destination ports to MIDI Server
  - Reference: USB MIDI Class 1.0 specification (usb.org)
- [ ] Implement virtio-sound driver for QEMU development testing
  - virtio-snd specification: virtio spec §5.14
  - Allows full audio stack testing without real hardware
- [ ] Write integration test: open HDA output from Device Server, write a
      440 Hz sine wave, verify no xruns

### MIDI Server (`servers/midi/`)

- [ ] Scaffold `servers/midi/` with `CMakeLists.txt` (userspace, links Mach stubs)
- [ ] Implement `midi_server.c` — main event loop; register
      `com.unhox.midi.server` with Bootstrap Server
- [ ] Implement `midi_device.c` — query Device Server for USB MIDI devices;
      create source/destination port pairs per physical port
- [ ] Implement `midi_uart.c` — legacy MPU-401 UART MIDI driver (for x86
      ISA MIDI port at 0x330); polling thread at `SCHED_NORMAL`
- [ ] Implement `midi_virtual.c` — virtual MIDI endpoint creation and teardown;
      any task can create a virtual source or destination
- [ ] Implement `midi_route.c` — routing table: source port → set of
      destination ports; timestamped event forwarding
  - Message ID range: 8300–8499 (see RFC-0005 §Message ID Ranges)
- [ ] Write `servers/midi/midi_mig.h` — all MIDI Server message structs
- [ ] Write unit test: create two virtual endpoints; send 100 MIDI note-on
      events; verify all arrive with correct timestamps
- [ ] Write unit test: route one source to three destinations; verify all
      three receive each event exactly once
- [ ] Verify milestone: USB MIDI keyboard events appear on a virtual
      destination port connected to a software synthesizer

### Audio Server (`servers/audio/`)

- [ ] Scaffold `servers/audio/` with `CMakeLists.txt`
- [ ] Implement `audio_server.c` — main(): register `com.unhox.audio.server`
      with Bootstrap Server; spawn management thread and RT I/O thread
- [ ] Implement `audio_device.c` — enumerate audio devices from Device Server;
      maintain logical device table; handle hot-plug notifications
- [ ] Implement `audio_session.c` — per-client session management:
  - `AUDIO_OP_OPEN_OUTPUT` (msg 8101): allocate stream port, OOL buffer region,
    reply with `stream_port`, `buffer_port`, `actual_frames`
  - `AUDIO_OP_OPEN_INPUT` (msg 8103): microphone / capture stream
  - `AUDIO_OP_CLOSE` (msg 8102): tear down session, reclaim buffer memory
- [ ] Implement `audio_format.c` — format negotiation (sample rate, channel
      count, bit depth); auto-insert `AU_TYPE_FORMAT_CONVERTER` nodes for
      mismatched streams
- [ ] Implement `audio_graph.c` — directed acyclic graph of Audio Unit nodes:
  - `graph_add_node` (msg 8201): instantiate an AU task; connect its ports
  - `graph_remove_node` (msg 8202): send `MACH_NOTIFY_NO_SENDERS` cleanup
  - `graph_connect` (msg 8203): connect output bus of one AU to input of another
  - `graph_disconnect` (msg 8204)
  - `graph_start` (msg 8205) / `graph_stop` (msg 8206)
  - Topological sort (Kahn's algorithm) for render order
- [ ] Implement `audio_rt.c` — `SCHED_RT` I/O thread:
  - Period = hardware buffer duration (e.g. 2.67 ms at 128 frames / 48 kHz)
  - Walk topological order; send `au_render_request` (msg 8501) to each node
  - Collect `au_render_reply`; timeout = period − safety margin (500 µs)
  - On timeout: insert silence for that node; increment xrun counter
  - Push final mix buffer to Device Server via HDA/USB stream port
- [ ] Write `servers/audio/audio_mig.h` — all Audio Server message structs
      (IDs 8000–8299)
- [ ] Write integration test: open output stream; write 1 s of 440 Hz sine
      wave via stream port; verify audio reaches Device Server with no xruns
- [ ] Benchmark: measure IPC latency at 128-frame / 48 kHz buffer (target < 1 ms
      end-to-end from RT thread wake to Device Server DMA submit)
- [ ] Verify milestone: Audio Server renders a two-node graph (sine AU →
      output AU) continuously for 10 s under QEMU with virtio-sound; zero xruns

### Audio Units Framework (`frameworks/AudioUnits/`)

- [ ] Scaffold `frameworks/AudioUnits/` with `CMakeLists.txt`
- [ ] Write `frameworks/AudioUnits/include/AudioUnit.h`:
  - AU type constants (`AU_TYPE_OUTPUT`, `AU_TYPE_MIXER`, `AU_TYPE_EFFECT`,
    `AU_TYPE_INSTRUMENT`, `AU_TYPE_FORMAT_CONVERTER`)
  - Render protocol structs (`au_render_request`, `au_render_reply`)
  - Scope and parameter types (`AU_SCOPE_GLOBAL`, `AU_SCOPE_INPUT`, …)
- [ ] Write `frameworks/AudioUnits/include/AUGraph.h` — client-side graph
      construction API (wraps Audio Server IPC, msg IDs 8200–8299)
- [ ] Write `frameworks/AudioUnits/include/MusicDevice.h` — instrument AU
      extensions: MIDI note-on/off, pitch bend, CC delivery
- [ ] Implement `AudioUnitBase.c` — boilerplate for third-party AU authors:
  - Port registration with Bootstrap Server
  - Default render loop (receives `au_render_request`, calls user callback,
    sends `au_render_reply`)
  - Default MIDI event receive loop (dispatches to user MIDI callback)
  - Default control parameter get/set (msg IDs 8700–8799)
- [ ] Implement `AUGraph.c` — client-side graph builder:
  - `AUGraphOpen()` — connect to Audio Server, get graph port
  - `AUGraphAddNode()` — `graph_add_node` IPC call
  - `AUGraphConnectNodeInput()` — `graph_connect` IPC call
  - `AUGraphStart()` / `AUGraphStop()`
- [ ] Implement system Audio Units (in-process, trusted):
  - `system/AUMixer.c` — N-channel mixer; float32 summing loop
  - `system/AUSampleRateConverter.c` — linear/sinc SRC (reference quality)
  - `system/AUOutput.c` — writes to Audio Server stream port
- [ ] Write unit test: instantiate a sine-wave generator AU; render 1024
      frames; verify output matches expected waveform to within −80 dB THD
- [ ] Write unit test: connect sine AU → EQ effect AU → mixer AU → output AU;
      render 1 s; verify signal flows end-to-end without distortion
- [ ] Write example program `tools/audio-test/sine_graph.c`:
  - Opens an output stream via Audio Server
  - Creates a two-node AUGraph (sine instrument → output)
  - Plays for 5 s then closes cleanly
  - Used as a smoke test for the full audio stack

### Documentation

- [x] Write `docs/rfcs/RFC-0005-audio-subsystem.md` — audio architecture RFC
- [x] Write `docs/audio-server-design.md` — comprehensive design document
- [x] Update `docs/architecture.md` — add audio/MIDI tier to stack diagram
- [ ] Write `servers/audio/README.md` — Audio Server overview and build instructions
- [ ] Write `servers/midi/README.md` — MIDI Server overview and build instructions
- [ ] Write `frameworks/AudioUnits/README.md` — AU framework usage guide
- [ ] Document SCHED_RT kernel interface in `docs/rfcs/RFC-0003-rt-scheduling.md`
- [ ] Document HDA driver design in `docs/rfcs/RFC-0004-hda-driver.md`

---

## Phase 6 — Desktop

- [ ] Prototype UNHOX Display Server (DPS-inspired, Mach IPC native)
- [ ] Build AppKit (libs-gui) with UNHOX display server backend
- [ ] Port GWorkspace as Workspace Manager
- [ ] Verify milestone v1.0: NeXT-heritage desktop boots

---

## Ongoing / Cross-Cutting

### Testing Infrastructure
- [ ] Set up CI: build kernel on push, run QEMU smoke test
- [ ] Kernel sanitizers: KASAN (address), KUBSAN (undefined behavior)
- [ ] Fuzz testing for IPC message parsing

### Documentation
- [ ] Document every design decision in `docs/rfcs/` before implementing
- [ ] Write historical context for each source in `archive/`
- [ ] Create contributor guide in `CONTRIBUTING.md`
- [ ] Write license inventory in `docs/sources.md`

### Kernel for QEMU Testing (near-term action)
- [ ] Set up dev environment — recommended: `nix develop` (see `tools/README.md`)
  - Alternative (Debian/Ubuntu): `sudo apt-get install qemu-system-x86_64 gcc-multilib grub-pc-bin grub-efi-amd64-bin xorriso`
  - Alternative (Fedora/RHEL): `sudo dnf install qemu-system-x86 grub2-tools xorriso`
  - Alternative (macOS/Homebrew): `brew install x86_64-elf-gcc qemu xorriso`
- [ ] Write minimal Multiboot2 kernel stub that prints "UNHOX" via serial
- [ ] Create `tools/qemu-run.sh`:
  ```sh
  qemu-system-x86_64 \
    -kernel kernel/build/unhox-kernel \
    -serial stdio \
    -display none \
    -m 256M \
    -no-reboot
  ```
- [ ] Test kernel stub boots and produces expected serial output
- [ ] Pull GNU Mach source as local reference:
  ```sh
  git clone https://git.savannah.gnu.org/git/hurd/gnumach.git archive/gnu-mach-ref
  ```
- [ ] Pull HURD source as local reference:
  ```sh
  git clone https://git.savannah.gnu.org/git/hurd/hurd.git archive/hurd-ref
  ```
