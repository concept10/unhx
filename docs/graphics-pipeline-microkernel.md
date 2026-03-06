# Graphics Pipeline Advances in a Microkernel OS

**Focus:** What shaders, ray tracing, AI/ML inference, and modern GPU pipeline
features mean for UNHOX — a Mach-inspired microkernel with Mach port IPC and
a planned L4-style IPC latency improvement roadmap.

---

## Table of Contents

1. [The GPU Pipeline — From Fixed-Function to Programmable](#1-the-gpu-pipeline--from-fixed-function-to-programmable)
2. [Shader Stages and the Modern Pipeline](#2-shader-stages-and-the-modern-pipeline)
3. [Compute Shaders and General-Purpose GPU (GPGPU)](#3-compute-shaders-and-general-purpose-gpu-gpgpu)
4. [Ray Tracing](#4-ray-tracing)
5. [AI / ML Inference on the GPU](#5-ai--ml-inference-on-the-gpu)
6. [Microkernel Architecture and the GPU](#6-microkernel-architecture-and-the-gpu)
7. [L4 IPC Latency Improvements and the Display Path](#7-l4-ipc-latency-improvements-and-the-display-path)
8. [UNHOX GPU Server Design](#8-unhox-gpu-server-design)
9. [Pipeline Feature Integration Roadmap for UNHOX](#9-pipeline-feature-integration-roadmap-for-unhox)
10. [References](#10-references)

---

## 1. The GPU Pipeline — From Fixed-Function to Programmable

### Fixed-Function Era (pre-2001)

Early GPUs (Voodoo, TNT2, GeForce 256) exposed a *fixed-function* pipeline:
transform, lighting, rasterise, texture, blend. Developers set parameters
(light colour, material, texture unit) but could not write custom programs.

```
Vertex data → T&L (Transform & Lighting) → Rasteriser → Texture → Framebuffer
```

### Programmable Shaders (2001–2006)

GeForce 3 (2001) introduced vertex programs. Pixel shaders appeared in
DirectX 8.0. "Shader Model" versions tracked increasing instruction counts
and precision:

- **SM 1.0** — 8 instructions, no branching, no loops
- **SM 2.0** — 64 instructions, static branching, 4 texture stages
- **SM 3.0** — 512+ instructions, dynamic branching, vertex texture fetch
- **SM 4.0** (D3D10) — geometry shaders, integer ops, unified shader model
- **SM 5.0** (D3D11) — compute shaders (CS), tessellation (hull/domain shaders)
- **SM 6.x** (D3D12/Vulkan) — wave intrinsics, mesh shaders, ray tracing (SM 6.5+)

### Unified Shader Architecture

Since AMD's R600 (2006) and NVIDIA's G80 (2006), all shader stages (vertex,
geometry, pixel, compute) run on the same ALU array. The driver schedules work
across the array dynamically. This means one programmer-visible execution unit
(a SIMD lane / CUDA core / shader processor) handles all workloads.

---

## 2. Shader Stages and the Modern Pipeline

### The Full Rasterisation Pipeline

```
┌──────────────────────────────────────────────────────────────────────────┐
│  CPU (API: Vulkan / D3D12 / Metal / OpenGL)                              │
│    └── command buffer: draw call, index buffer, descriptor sets          │
├──────────────────────────────────────────────────────────────────────────┤
│  Vertex Shader (VS)                                                      │
│    Input:  vertex attributes (position, normal, UV, ...)                 │
│    Output: clip-space position + interpolants                            │
├──────────────────────────────────────────────────────────────────────────┤
│  [Optional] Hull Shader (HS) + Tessellator + Domain Shader (DS)          │
│    Subdivides patches dynamically based on screen-space LOD              │
├──────────────────────────────────────────────────────────────────────────┤
│  [Optional] Geometry Shader (GS)                                         │
│    Generates new primitives (deprecated in favour of mesh shaders)       │
├──────────────────────────────────────────────────────────────────────────┤
│  Rasteriser                                                              │
│    Converts triangles to fragments; interpolates attributes              │
├──────────────────────────────────────────────────────────────────────────┤
│  Fragment / Pixel Shader (PS)                                            │
│    Computes final colour per fragment; texture sampling; lighting        │
├──────────────────────────────────────────────────────────────────────────┤
│  Output Merger (ROP — Render Output Unit)                                │
│    Depth test, stencil test, blending → writes to framebuffer            │
└──────────────────────────────────────────────────────────────────────────┘
```

### Mesh Shaders (SM 6.5+, Ampere/RDNA 2)

Mesh shaders collapse the VS/HS/DS/GS chain into two stages:

- **Task Shader (amplification):** dispatches mesh groups
- **Mesh Shader:** outputs indexed vertex/primitive data directly

Advantages: arbitrary output topologies, per-primitive culling in the shader,
no vertex pre-processing bottleneck. Meshlets (small mesh chunks of ~128
triangles) are the fundamental unit, enabling hierarchical visibility culling.

---

## 3. Compute Shaders and General-Purpose GPU (GPGPU)

A compute shader (Vulkan: `VkPipeline` of type `COMPUTE`; D3D12: `ID3D12PipelineState`
compute; Metal: `MTLComputePipelineState`) runs on the GPU's shader array without
rasterisation. Work is dispatched in 3D grids of thread groups.

### GPU Parallelism Model

```
Dispatch(X, Y, Z)  →  X * Y * Z thread groups
Each thread group  →  N threads (CUDA: warp of 32; AMD: wavefront of 64)
Each thread        →  executes the compute shader program
```

Threads within a group share:
- **Shared memory / LDS (Local Data Store):** fast scratchpad (48–96 KB)
- **Barriers:** `barrier()` / `GroupMemoryBarrierWithGroupSync()`
- **Wave intrinsics:** `WaveReadLaneAt`, `WaveActiveSum`, shuffle, vote

### GPGPU Uses in a Display Server

| Task | Shader Type | Notes |
|------|-------------|-------|
| Compositing (blend surfaces) | Fragment or Compute | Alpha blending, colour space |
| Font rasterisation | Compute | SDF generation, hinting |
| Image decode (JPEG, PNG, HEIF) | Compute | Faster than CPU for large assets |
| Video decode | Fixed-function (NVDEC/VCN) | Dedicated decode unit |
| Shadow maps | Vertex + Fragment | Geometry: shadow volumes |
| Post-processing (blur, tonemap) | Compute | Ping-pong textures |
| AI upscaling (DLSS/FSR) | Compute | Neural network or spatial filter |

---

## 4. Ray Tracing

### Hardware Ray Tracing (RTX / RDNA 2)

NVIDIA Turing (2018) introduced RT Cores — fixed-function hardware for
*bounding volume hierarchy (BVH) traversal* and *ray-triangle intersection*.
AMD RDNA 2 (2020) added hardware ray accelerators.

The API surface (Vulkan Ray Tracing / D3D12 DXR):

```
Acceleration Structure (AS)
  ├── Bottom-Level AS (BLAS): geometry (triangles / AABBs) per mesh
  └── Top-Level AS (TLAS): instances of BLASes with transforms

Ray Tracing Pipeline:
  ├── Ray Generation Shader — generates rays (e.g. from camera)
  ├── Intersection Shader — custom primitive intersection (optional)
  ├── Any-Hit Shader — called for every candidate hit (for alpha test)
  ├── Closest-Hit Shader — called for the nearest hit
  └── Miss Shader — called when no hit found
```

### Software Ray Tracing (Lumen, Probe-Based)

Unreal Engine 5's Lumen uses:
- **Screen Space** + **Signed Distance Fields (SDFs)** for short-range GI
- **Hardware ray tracing** (Vulkan RT) for mid/long-range

Mesa's `radv` + `tu` (turnip) implement Vulkan ray tracing on AMD and Qualcomm
Adreno GPUs.

### Ray Tracing in a Display Server / OS Context

Ray tracing is primarily a content rendering feature, but the OS display server
can leverage it for:

1. **Cursor rendering:** accurate per-pixel cursor shapes with lighting
2. **Window shadow rendering:** physically correct soft shadows (demo value, but
   NEXTSTEP had pixel-perfect shadows without ray tracing)
3. **ML-accelerated compositing:** see §5

More practically, a microkernel OS that exposes GPU ray tracing correctly gives
application frameworks (AppKit, game engines) full access to RTX features via
the GPU device server's Vulkan interface.

---

## 5. AI / ML Inference on the GPU

### Tensor Cores (NVIDIA) and Matrix Engines (AMD, Intel)

Modern GPUs include dedicated matrix multiply units:

- **NVIDIA Tensor Cores** (Volta, 2017+): 4×4 or 16×16 WMMA ops; FP16/BF16/INT8/FP8
- **AMD Matrix Cores** (CDNA/RDNA 3+): MFMA instructions; FP64/FP32/FP16/BF16/INT8
- **Intel XMX** (Xe, Arc): systolic array; INT8/FP16

These units execute `D = A × B + C` with dimensions in the 16–64 range per
clock per SM/CU, at a much higher throughput than the scalar/vector ALU.

### Inference Workloads Relevant to an OS / Desktop

| Feature | Model Type | GPU Resources | Notes |
|---------|-----------|---------------|-------|
| DLSS 3 (NVIDIA) | CNN / Transformer | Tensor Cores | AI upscaling; trades res for quality |
| FSR 4 (AMD) | ML filter | Matrix Cores | Open super-resolution |
| DirectSR (MSFT) | Abstraction | Any HW SR | Delegates to DLSS/FSR/XeSS |
| Text rendering (font hinting) | Small MLP | Compute | Learned hinting (future) |
| Image compression (AVIF, AVM) | CNN-inloop filter | Compute | Encode/decode assist |
| OCR / accessibility | Transformer | Compute | Screen reader at OS level |
| NLP at OS level | LLM (quantised) | All matrix units | Inference service (e.g. Copilot) |
| Noise suppression (audio) | RNN | Compute | Microphone pre-processing |

### Inference Frameworks and APIs

- **ONNX Runtime** — vendor-neutral inference; has CUDA, DirectML, CoreML EPs
- **TensorRT** (NVIDIA) — high-performance compiled inference for NVIDIA GPUs
- **MIGraphX / ROCm** (AMD) — equivalent open-source stack
- **OpenVINO** (Intel) — inference on Intel GPU/NPU/CPU
- **DirectML** (Microsoft) — D3D12-based inference; GPU-vendor-neutral on Windows
- **Metal Performance Shaders** (Apple) — includes MPSGraph for inference

For UNHOX, the pragmatic path is:
1. Expose the GPU via a Vulkan-capable device server
2. Run ONNX Runtime (Vulkan EP or CPU fallback) as a userspace inference service
3. Expose inference to applications via a Mach port service interface

---

## 6. Microkernel Architecture and the GPU

### The Core Problem: Privileged GPU Access

GPUs need to:
1. Allocate GPU-accessible memory (VRAM and shared system memory)
2. Map physical GPU memory into a process's virtual address space
3. Submit command buffers to the GPU's DMA engine via IOCTL
4. Receive GPU interrupts (completion, page fault, pre-emption)

In a monolithic kernel, all of this is handled by the GPU kernel module
(`amdgpu.ko`, `nvidia.ko`, `i915.ko`). In a microkernel, the kernel module
must be minimised or eliminated.

### Driver Architecture Options in a Microkernel

#### Option A: GPU Driver in Device Server (Preferred for UNHOX)

```
Application Task
  │  Vulkan API call (libvulkan.so, Mesa/RADV — in process)
  │  → user-mode driver: builds command buffer in CPU memory
  │  mach_msg(SEND, gpu_server_port, submit_cmd_buf_msg)
  ▼
GPU Device Server Task
  │  Validates command buffer
  │  Maps OOL memory as GPU-accessible
  │  Submits to GPU ring via kernel IOCTL stub
  ▼
Kernel GPU Stub (minimal kernel code)
  │  Provides: memory map, ring submit, interrupt delivery
  │  No parser, no state machine — just hardware access
  ▼
GPU Hardware
```

This is structurally identical to Windows WDDM (user-mode driver + kernel stub)
but the "user-mode driver" runs in a separate server task, not in the application's
process. This gives:
- **Isolation:** GPU driver crash kills the device server, not the application
- **Capability control:** applications must hold a Mach send right to the GPU server
- **Restart:** the device server can be restarted without rebooting (watchdog)

#### Option B: GPU Driver in Application Process (DRI / Mesa model)

Mesa's DRI model runs most of the driver in the application's process. The kernel
only provides DRM (memory allocation, command buffer submission). In a microkernel:

- The `drm` kernel interface becomes an IPC interface to a minimal kernel stub
- Mesa (in-process) builds command buffers and calls `mach_msg` to the stub
- Lower IPC overhead but less isolation

#### Option C: Full GPU Virtualisation (IOMMU-based)

For cloud / VM scenarios: GPU SRIOV virtual functions + VFIO expose separate GPU
contexts per VM. In a microkernel, each task could be granted an SRIOV virtual
function as a capability. This is complex but provides hardware-level isolation.

### The IPC Bottleneck

The dominant concern when putting GPU access behind IPC is latency. A Mach IPC
round-trip (send + receive) has historically been ~5–20 µs in CMU Mach 3.0.
Modern L4 microkernels (seL4, Fiasco.OC, OKL4) achieve ~0.2–0.5 µs IPC.

For a GPU command submission path:
- Command buffer recording is done in user space (no IPC)
- IPC only for: submit, sync, memory allocation — infrequent operations
- Per-frame: 1–3 submits × 1 µs IPC overhead = ~3 µs/frame at 60 fps

At 60 fps, 1 frame = 16.67 ms. 3 µs IPC overhead = 0.018% of frame budget.
Even at Mach 3.0 latency (20 µs × 3 = 60 µs = 0.36% of budget), IPC is not
the bottleneck for GPU work.

---

## 7. L4 IPC Latency Improvements and the Display Path

### L4 IPC Generations

| Generation | Kernel | IPC Latency | Key Innovation |
|---|---|---|---|
| L4 v2 (Liedtke, 1993) | L4/486 | 5 µs | Avoid all unnecessary state; register-based IPC |
| L4Ka::Pistachio | L4Ka | 2–4 µs | UTCB (User-level Thread Control Block) |
| NOVA | Stein/Härtig | 0.5–1 µs | Typed IPC caps, no UTCB copying |
| seL4 | NICTA (2009) | 0.2–0.5 µs | Formally verified; minimal kernel object set |
| Fiasco.OC | TU Dresden | 0.3–0.8 µs | L4Re userland; IOMMU integration |

Mach 3.0 IPC: ~5–20 µs (Draves et al., 1991 benchmark). The cost of Mach's
richer message format (typed descriptors, port right transfer) was ~10–40×
versus L4 IPC.

### What Makes L4 IPC Fast

1. **Register-based message passing:** Small messages fit in CPU registers;
   no memory copies. L4 passes message words in `%eax`, `%ecx`, `%edx` etc.
2. **Direct process switch:** The IPC system call directly schedules the receiver
   without going through a scheduler (synchronous RPC path).
3. **No intermediate buffering:** No kernel message queue for synchronous IPC;
   the kernel copies directly from sender's registers/UTCB to receiver.
4. **Minimal kernel objects:** No heavyweight port object; just a capability (ID).
5. **Cache-warm paths:** The kernel IPC path fits in L1 cache.

### UNHOX IPC Improvement Roadmap

UNHOX currently implements Mach 3.0-style IPC (kernel message queues, port
objects, typed descriptors). Planned improvements toward L4 performance:

1. **Phase 2:** Combined send+receive trap (`mach_msg` with both `MACH_SEND_MSG`
   and `MACH_RCV_MSG`) — avoids two separate context switches for RPC pattern.
   Already stubbed in `kernel/ipc/mach_msg.c`.

2. **Phase 3:** UTCB-style fast path — for small messages (≤4 words), pass in
   registers and avoid kernel heap allocation (`ipc_kmsg` alloc).

3. **Phase 4:** Direct scheduling on IPC — when a server is waiting on a port,
   sending a message to that port directly switches to the server thread
   (continuation-based scheduling, as in Mach 3.0 continuations).

4. **Phase 5:** Asynchronous notification ports (knotes/kqueue equivalent) for
   GPU completion events — application posts work via IPC, GPU server delivers
   completion via an async notification message without waking the client.

### Impact on the Display Path

With L4-style IPC latency:

```
Wayland (Unix socket, Linux):    ~2–5 µs per message (socket overhead)
UNHOX Mach IPC (current):        ~5–20 µs per message
UNHOX Mach IPC (Phase 3 fast):   ~0.5–2 µs per message (register path)
UNHOX Mach IPC (Phase 5 async):  ~0.1–0.5 µs (notification, one-way)
```

At Phase 3, UNHOX IPC matches or beats Wayland Unix socket latency.
The display server's frame pipeline:

```
App: render (GPU, async)
  → IPC: attach buffer (0.5 µs)
  → Display server: composite (GPU, async)
  → IPC: present notification (0.1 µs async)
  → DRM/KMS: scanout
```

Total IPC overhead per frame: ~0.6 µs at Phase 3. Frame budget at 60 fps:
16.67 ms. IPC overhead: 0.004%.

---

## 8. UNHOX GPU Server Design

### Component Map

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Application Task                                                        │
│    libvulkan.so → Mesa RADV (in-process UMD)                            │
│    ├── Records command buffer in CPU memory (no IPC)                     │
│    └── mach_msg(SEND, gpu_port, submit_msg) → GPU Server               │
├─────────────────────────────────────────────────────────────────────────┤
│  Display Server Task (frameworks/DisplayServer/)                        │
│    ├── Owns wl_display equivalent (Mach port set)                       │
│    ├── Composites client surfaces using Vulkan compute pass              │
│    ├── mach_msg(SEND, gpu_port, submit_msg) → GPU Server               │
│    └── mach_msg(SEND, drm_port, scanout_msg) → Device Server           │
├─────────────────────────────────────────────────────────────────────────┤
│  GPU Device Server Task (servers/device/ — GPU subsystem)               │
│    ├── Validates command buffers (security critical)                     │
│    ├── Manages GPU VA space (per-task VA ranges)                        │
│    ├── Submits to hardware ring via kernel IOCTL stub                   │
│    └── Delivers completion notifications (async Mach messages)          │
├─────────────────────────────────────────────────────────────────────────┤
│  UNHOX Kernel — GPU stub                                                │
│    ├── mach_vm_allocate with GPU-accessible flags (VRAM mapping)        │
│    ├── gpu_ring_submit() — writes to hardware doorbell register         │
│    └── Interrupt handler → mach_msg to GPU Device Server               │
├─────────────────────────────────────────────────────────────────────────┤
│  GPU Hardware (AMDGPU / Intel integrated / virtio-gpu in QEMU)          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Security Model

Each application receives a **GPU context capability** — a Mach send right to
a port representing its GPU context in the device server. The server:

1. Validates command buffers before submission (prevent GPU ring hijack)
2. Enforces VA range isolation (no cross-context GPU memory access)
3. Pre-empts long-running GPU jobs (GPU scheduler in device server)
4. Delivers GPU page fault notifications to the owning task

This is structurally equivalent to AMD's per-process VM (PASID/IOMMU) combined
with the WDDM validation layer.

### Buffer Sharing Protocol

Client → Display Server buffer handoff via Mach OOL memory:

```c
/* Client sends: */
struct surface_buffer_msg {
    mach_msg_header_t        header;
    mach_msg_ool_descriptor_t gpu_buf;   /* shared GPU memory handle */
    uint32_t                 width;
    uint32_t                 height;
    uint32_t                 format;     /* BGRA8888, etc.           */
    uint64_t                 gpu_fence;  /* completion fence value   */
};
```

The `gpu_buf` OOL descriptor carries a Mach memory entry for the GPU-accessible
surface. The display server maps it into its own address space without a copy.
The `gpu_fence` allows the compositor to wait for the client's GPU work to
complete before compositing (timeline semaphore / sync_file equivalent).

---

## 9. Pipeline Feature Integration Roadmap for UNHOX

### Phase 5: Display Server — Software Compositor

```
Target: virtio-gpu in QEMU → software Mesa (llvmpipe/softpipe)
API: OpenGL ES 3.1 (Mesa softpipe) or Vulkan 1.1 (Mesa lvp — lavapipe)
Features: basic compositing, DPS drawing model, Mach IPC protocol
```

- [ ] `servers/device/gpu/` — minimal DRM interface over Mach IPC
- [ ] `frameworks/DisplayServer/compositor/` — Vulkan compute compositor
- [ ] `frameworks/DisplayServer/protocol/` — Mach IPC protocol (display_msg.h)
- [ ] `frameworks/AppKit/` backend adapter for UNHOX display server

### Phase 6: Hardware GPU Acceleration

```
Target: AMD GPU (AMDGPU + Mesa RADV) or Intel integrated (ANV)
API: Vulkan 1.3
Features: hardware accelerated compositing, GPU buffer sharing, explicit sync
```

- [ ] `servers/device/gpu/amdgpu/` — AMDGPU command submission
- [ ] GPU VA allocator in device server
- [ ] OOL GPU buffer sharing (dma-buf equivalent via Mach memory entries)
- [ ] Explicit GPU fence delivery via async Mach messages

### Phase 7: Ray Tracing and AI/ML

```
Target: NVIDIA RTX or AMD RDNA 2+ (VK_KHR_ray_tracing_pipeline)
ML: ONNX Runtime (Vulkan EP) as a Mach port service
Features: RTX shadows/reflections in DPS compositor; DLSS/FSR upscaling
```

- [ ] `servers/inference/` — ONNX Runtime inference server (Mach IPC)
- [ ] Vulkan RT acceleration structure management in GPU device server
- [ ] Display server: optional AI upscaling pass (DLSS/FSR-style)
- [ ] `docs/rfcs/RFC-0003-gpu-inference-service.md`

### Phase 8: Mesh Shaders and Nanite-Scale Detail

```
Target: SM 6.5+ GPU (VK_EXT_mesh_shader)
Features: meshlet-based scene rendering in application frameworks
```

- [ ] Meshlet management API in GPU device server
- [ ] Task/mesh shader support in UNHOX Vulkan layer

---

## 10. References

### GPU Architecture
- Patterson & Hennessy, "Computer Architecture: A Quantitative Approach" — GPU ch.
- NVIDIA, "Turing GPU Architecture" (2018) — whitepaper
- AMD, "RDNA 3 Architecture" (2022) — whitepaper
- Intel, "Xe HPG Microarchitecture" (2021) — whitepaper

### Vulkan and Ray Tracing
- Khronos, "Vulkan Ray Tracing Best Practices" (2021)
- Shirley & Morley, "Realistic Ray Tracing" (2003)
- Akenine-Möller et al., "Real-Time Rendering, 4th Ed." (2018) — Ch. 26 (RT)

### AI/ML on GPU
- NVIDIA, "TensorRT Developer Guide" (2023)
- Recht et al., "A Tour of Machine Learning for Systems Programmers" (2022)
- ONNX Runtime docs — https://onnxruntime.ai

### Microkernel and IPC Performance
- Liedtke, "On µ-Kernel Construction" (SOSP 1995)
- Draves, Bershad et al., "Using Continuations to Implement Thread Management
  and Communication in Operating Systems" (SOSP 1991)
- Heiser et al., "L4 Microkernels: The Lessons from 20 Years of Research and
  Deployment" (Operating Systems Review 2016)
- Klein et al., "seL4: Formal Verification of an OS Kernel" (SOSP 2009)

### UNHOX-Specific
- `docs/display-server-architectures.md` — display server survey
- `docs/ipc-design.md` — UNHOX IPC design
- `docs/rfcs/RFC-0002-display-server-architecture.md` — UNHOX display server RFC
- `kernel/ipc/mach_msg.c` — combined send+receive trap (L4-style RPC)
