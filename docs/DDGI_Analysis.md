# DDGI Implementation — Analysis & Plan

> **Phase:** DDGI (RTXGI Dynamic Diffuse Global Illumination) — standalone implementation phase  
> **Goal:** Integrate RTXGI-DDGI SDK for baked/live indirect diffuse lighting (probe ray tracing, indirect query), blended with SSGI for high-frequency detail

---

## Table of Contents

1. [Codebase Architecture Overview](#1-codebase-architecture-overview)
2. [RenderingMode Enum & CommonConsts Changes](#2-renderingmode-enum--commonconsts-changes)
3. [Render Pass Scheduling](#3-render-pass-scheduling)
4. [RTXGI-DDGI — Deep Dive](#4-rtxgi-ddgi--deep-dive)
5. [Feature Dependency Map & Disabled Features](#5-feature-dependency-map--disabled-features)
6. [DeferredRenderer Modifications](#6-deferredrenderer-modifications)
7. [Transparent Lighting in NormalBasic](#7-transparent-lighting-in-normalbasic)
8. [Implementation Roadmap](#8-implementation-roadmap)
9. [DDGI + SSGI Hybrid Blending](#9-ddgi--ssgi-hybrid-blending)
- [Appendix A: File Index](#appendix-a-file-index)
- [Appendix B: Key Design Decisions](#appendix-b-key-design-decisions)

---

## 1. Codebase Architecture Overview

### 1.1 Rendering Pipeline

The current pipeline uses a modular render-graph architecture:

```
Per Frame:
  RenderGraph::Reset()
  RenderGraph::BeginSetup()
  for each IRenderer:
      IRenderer::Setup(RenderGraph)  ← declares resources
      RenderGraph::ScheduleRenderer()
  RenderGraph::EndSetup()
  RenderGraph::Compile()             ← resolves lifetimes, aliases resources
  TaskScheduler::ExecuteAllScheduledTasks()  ← parallel Render()
  RenderGraph::PostRender()
```

### 1.2 Current Render Pass Order (Normal Mode)

Found in [Renderer.cpp](../src/Renderer.cpp) ~lines 817-848:

```
ClearRenderer → TLASRenderer → OpaqueRenderer → MaskedPassRenderer
→ HZBGeneratorPhase2 → RTXDIRenderer → SHARCRenderer
→ DeferredRenderer → SkyRenderer → TransparentPassRenderer
→ TAARenderer → BloomRenderer → HDRRenderer → ImGuiRenderer
```

### 1.3 Key Feature Flags (Renderer struct)

From [Renderer.h](../src/Renderer.h) lines 221-310:

| Flag | Type | Default | Purpose |
|---|---|---|---|
| `m_Mode` | `RenderingMode` | `Normal` | Current rendering mode |
| `m_EnableRTShadows` | `bool` | `true` | RT shadows — **already replaced by CSM in earlier phase** |
| `m_EnableReSTIRDI` | `bool` | `true` | ReSTIR direct illumination — **disabled in NormalBasic** |
| `m_EnableReSTIRDenoising` | `bool` | `true` | NRD REBLUR denoising — **disabled in NormalBasic** |
| `m_IndirectLightingTechnique` | `uint32_t` | `2` (SHARC) | 0=None, 1=ReSTIR GI, 2=SHARC — **set to 0 in NormalBasic** |
| `m_EnableDDGI` | `bool` | `false` | **NEW** — controls DDGI probe RT + blending (bake mode when off) |
| `m_EnableOcclusionCulling` | `bool` | `true` | 2-phase HZB occlusion |

### 1.4 Dependencies

| Feature | Where Used | Dependency Chain |
|---|---|---|
| **ReSTIR DI** | `RTXDIRenderer`, `CompositingPass` | ReSTIR DI → NRD → Denoised output → DeferredRenderer |
| **ReSTIR GI** | `RTXDIRenderer` (GI pass) | ReSTIR GI → NRD → Denoised output |
| **SHARC** | `SHARCRenderer` (Update/Resolve/Query) | SHARC → g_RG_SHARCIndirect → DeferredRenderer |
| **OMM** | `RTXDIRenderer` (BLAS with OMM) | OMM → BLAS build (RT only) |
| **NRD** | `NrdIntegration` → `RTXDIRenderer` | NRD denoising for ReSTIR DI & GI outputs |
| **HWRT Shadows** | `DeferredRenderer` | **Already replaced by CSM** (ShadowRenderer + ShadowMaskRenderer) in prior phase |
| **TLAS** | `TLASRenderer`, `RTXDIRenderer`, `PathTracerRenderer` | **Still needed for DDGI probe rays** when `m_EnableDDGI=true`; not needed in bake mode |

**Key Insight:** DDGI is the _only_ remaining consumer of TLAS in NormalBasic. When `m_EnableDDGI=false` (bake mode), TLAS can be skipped entirely — pure raster pipeline.

---

## 2. RenderingMode Enum & CommonConsts Changes

### 2.1 Current State (NormalBasic implemented ✅)

**Host-side** ([Renderer.h](../src/Renderer.h) line 83):
```cpp
enum class RenderingMode : uint32_t
{
    Normal = srrhi::CommonConsts::RENDERING_MODE_NORMAL,                      // = 0
    IBL = srrhi::CommonConsts::RENDERING_MODE_IBL,                            // = 1
    ReferencePathTracer = srrhi::CommonConsts::RENDERING_MODE_PATH_TRACER,    // = 2
    NormalBasic = srrhi::CommonConsts::RENDERING_MODE_NORMAL_BASIC            // = 3
};
```

**Shader-side** ([Common.sr](../src/shaders/Common.sr) line 86):
```hlsl
static const int RENDERING_MODE_NORMAL = 0;
static const int RENDERING_MODE_IBL = 1;
static const int RENDERING_MODE_PATH_TRACER = 2;
static const int RENDERING_MODE_NORMAL_BASIC = 3;
```

### 2.2 Completed Changes ✅

All changes from the CSM phase are done:

1. ✅ `NormalBasic` enum value added to [Renderer.h](../src/Renderer.h) and [Common.sr](../src/shaders/Common.sr).
2. ✅ `NormalBasic` scheduling branch in [Renderer.cpp](../src/Renderer.cpp) skips TLAS, RTXDI, SHARC.
3. ✅ `--normalbasic` CLI flag in [Config.cpp](../src/Config.cpp) auto-disables RT feature flags.
4. ✅ BLAS/TLAS building skipped in NormalBasic (empty placeholder TLAS created for binding compatibility).
5. ✅ Mode-switch assert: switching NormalBasic → Normal/PathTracer asserts TLAS is built. IBL does not assert.

---

## 3. Render Pass Scheduling — Current vs NormalBasic

### 3.1 New Scheduling Logic (Pseudocode)

```cpp
// In Renderer::ScheduleAndRunAllRenderers()
if (m_Mode == RenderingMode::ReferencePathTracer)
{
    // ... existing path tracer path ...
}
else if (m_Mode == RenderingMode::NormalBasic)
{
    // NormalBasic: classic rasterization pipeline
    m_RenderGraph.ScheduleRenderer(g_OpaqueRenderer);
    m_RenderGraph.ScheduleRenderer(g_MaskedPassRenderer);
    m_RenderGraph.ScheduleRenderer(g_HZBGeneratorPhase2);
    // CSM shadows (already implemented in prior phase)
    m_RenderGraph.ScheduleRenderer(g_ShadowRenderer);       // CSM depth-only pass
    m_RenderGraph.ScheduleRenderer(g_ShadowMaskRenderer);   // fullscreen shadow mask compute
    // NEW — DDGI (this phase)
    if (g_Renderer.m_EnableDDGI)
        // Schedule TLAS if DDGI is active (probe rays need it)
        m_RenderGraph.ScheduleRenderer(g_TLASRenderer);
    m_RenderGraph.ScheduleRenderer(g_DDGIRenderer);          // probe RT+blend (if enabled) + indirect query (always)
    //
    m_RenderGraph.ScheduleRenderer(g_DeferredRenderer);
    m_RenderGraph.ScheduleRenderer(g_SkyRenderer);
    m_RenderGraph.ScheduleRenderer(g_TransparentPassRenderer);
    m_RenderGraph.ScheduleRenderer(g_TAARenderer);
    m_RenderGraph.ScheduleRenderer(g_BloomRenderer);
}
else  // NormalAdvanced (old Normal) + IBL
{
    // existing full pipeline with RTXDI, SHARC, TLAS
    ...
}
```

### 3.2 Pass Comparison Matrix

| Pass | NormalAdvanced | DDGI Phase (NormalBasic) | Notes |
|---|---|---|---|
| ClearRenderer | ✅ | ✅ | |
| TLASRenderer | ✅ | ✅ (conditional) | Only scheduled when `m_EnableDDGI=true` |
| OpaqueRenderer | ✅ | ✅ | |
| MaskedPassRenderer | ✅ | ✅ | |
| HZBGeneratorPhase2 | ✅ | ✅ | |
| **ShadowRenderer** | ❌ | ✅ | CSM depth-only pass (prior phase) |
| **ShadowMaskRenderer** | ❌ | ✅ | Fullscreen shadow mask compute (prior phase) |
| **DDGIRenderer** | ❌ | ✅ | **NEW** — probe RT + blend + indirect query |
| RTXDIRenderer | ✅ | ❌ | Disabled |
| SHARCRenderer | ✅ (if enabled) | ❌ | Disabled |
| DeferredRenderer | ✅ (RT shadows) | ✅ (shadow mask + DDGI indirect) | Reads shadow mask + DDGI output |
| SkyRenderer | ✅ | ✅ | |
| TransparentPassRenderer | ✅ (with TLV) | ✅ (simplified) | No TLV; simplified forward with DDGI |
| TAARenderer | ✅ | ✅ | |
| BloomRenderer | ✅ | ✅ | |
| HDRRenderer | ✅ | ✅ | |
| ImGuiRenderer | ✅ | ✅ | |

---

## 4. RTXGI-DDGI — Deep Dive

### 4.1 How DDGI Works (From Reference Study)

DDGI (Dynamic Diffuse Global Illumination) uses a **3D grid of probes** placed in world space. Each probe stores:
- **Irradiance** (octahedral map — sphere unwrapped to 2D)
- **Distance** (octahedral map — mean & variance of distance to surfaces)

**Per-frame pipeline:**
1. **Update Constants** — random rotation for probe rays, scroll offsets
2. **Trace Probe Rays** — ray trace from each probe, gather radiance & distance (APPLICATION responsibility)
3. **Probe Blending** — blend new ray data into irradiance/distance textures (SDK handles)
4. **Probe Relocation** *(optional)* — move probes out of geometry (SDK handles)
5. **Probe Classification** *(optional)* — deactivate probes in empty space (SDK handles)
6. **Probe Variability** *(optional)* — measure convergence; stop when stable (SDK handles)
7. **Query Irradiance** — fullscreen pass gathers indirect lighting from probes (APPLICATION responsibility)

### 4.2 Reference Sample Analysis

The test harness ([DDGI_D3D12.cpp](../REFERENCES/RTXGI-DDGI/samples/test-harness/src/graphics/DDGI_D3D12.cpp)) shows:
- **Single large volume** covering the entire map (Cornell Box / Sponza)
- **Unmanaged resource mode** — application creates textures, SDK creates PSOs
- **Bindless resource access** — resources accessed via descriptor heap indices
- **Probe ray tracing** uses inline ray tracing (`RayQuery`) in a compute shader, not a ray generation shader. This avoids DXR hit groups and state objects entirely — all DDGI RT is done from a pure CS with a single `Dispatch()` call.
- **Indirect lighting query** in a fullscreen compute shader ([IndirectCS.hlsl](../REFERENCES/RTXGI-DDGI/samples/test-harness/shaders/IndirectCS.hlsl))

**Key SDK API calls:**
```
DDGIVolume::Create(desc, resources)
DDGIVolume::Update()                          // pre-trace: random rotation
GetRayDispatchDimensions()                    // get dispatch size for probe rays
rtxgi::d3d12::UpdateDDGIVolumeProbes()        // blend irradiance & distance
rtxgi::d3d12::RelocateDDGIVolumeProbes()      // optional: move probes
rtxgi::d3d12::ClassifyDDGIVolumeProbes()      // optional: deactivate probes
DDGIGetVolumeIrradiance()                     // shader function: query indirect
DDGIGetVolumeBlendWeight()                    // shader function: blend volumes
```

### 4.3 Texture Formats

DDGI uses these texture arrays:

| Resource | Format | Dimensions | Notes |
|---|---|---|---|
| Ray Data | RGBA32_FLOAT or RG32_FLOAT | `(probesPerPlane, numRays, numPlanes)` | Temporary, per-frame |
| Irradiance | R10G10B10A2_UNORM or RGBA16_FLOAT | `(probeTexels * probesX, probeTexels * probesZ, probesY)` | Persistent, gamma-encoded |
| Distance | RG16_FLOAT | `(probeTexels * probesX, probeTexels * probesZ, probesY)` | Persistent |
| Probe Data | RGBA16_FLOAT | `(probesX, probesZ, probesY)` | Offsets + classification |
| Variability | R16_FLOAT | Like irradiance (no border) | Per-texel coefficient of variation |
| Variability Avg | RG16_FLOAT | Reduction pyramid | Average across volume |

### 4.4 Automatic Scene-Driven Volume Placement

Volumes are placed automatically from scene geometry at load time. See §4.12 for the full algorithm (voxelization, PCA-OBB fitting, greedy selection). This replaces the earlier ISV-based manual placement strategy with a scene-adaptive approach that minimizes probes in empty space.

### 4.5 Volume Culling

**Q: Frustum culling? Occlusion culling for volumes?**

**A:** 
- **Frustum culling:** `DDGIVolume::GetAxisAlignedBoundingBox()` → test against camera frustum. If volume is outside frustum, skip probe updates for that frame.
- **Occlusion culling:** More complex. Can use the HZB from the main camera pass. If all probes in a volume are behind occluders (as determined by HZB), skip the volume. **Not recommended for initial implementation** — frustum culling is sufficient.

### 4.6 Variability & Convergence

**Q: When to stop dispatching rays? When probe results are "stable"?**

**A:** The SDK's `CalculateDDGIVolumeVariability()` and `ReadbackDDGIVolumeVariability()` provide a **coefficient of variation** across all probes. When this drops below a threshold (e.g., 0.01-0.05), the volume has "converged enough."

```
if (variability < 0.03 && !sceneChanged)
    skip_probe_updates = true;
```

However, in practice for a real-time game:
- Probes **never fully converge** because the camera moves
- With multiple scene-driven volumes, new probes are constantly being introduced at volume edges as the camera moves
- **Recommendation:** Always update probes for active volumes; only use variability for optional stationary volumes

### 4.7 Indirect Diffuse vs Specular

**Q: Is DDGI only for indirect diffuse? What about indirect specular?**

**A:** DDGI is **diffuse-only**. It stores irradiance (hemispherical integral of incoming radiance weighted by cos θ). For specular indirect:
- Screen-space reflections (SSR) — cheap, works in NormalBasic
- Ray-traced reflections — requires RT support (not in NormalBasic scope)
- **Recommendation:** DDGI for indirect diffuse + SSR (if desired) for indirect specular

### 4.8 Non-Volume Areas (Far Distance)

**Q: What about indirect lighting for areas outside volumes?**

**A:** The SDK provides `DDGIGetVolumeBlendWeight()` which returns 1.0 inside the volume and decays to 0 outside based on distance. Use this to blend with:
- A constant ambient color (simple)
- A sky-derived ambient term (better — sample sky irradiance)
- An IBL probe (best but needs environment map)

For NormalBasic: blend with sky ambient outside DDGI volume. This is what the test harness does.

### 4.9 DDGI Integration Architecture for HobbyRenderer

```
class DDGIRenderer : public IRenderer
{
    // Per-volume state
    struct DDGIVolumeState
    {
        rtxgi::d3d12::DDGIVolume volume;
        rtxgi::d3d12::DDGIVolumeResources resources;
        rtxgi::DDGIVolumeDesc desc;
        
        // HobbyRenderer-specific texture handles (for render graph integration)
        RGTextureHandle irradianceTex;
        RGTextureHandle distanceTex;
        RGTextureHandle probeDataTex;
        RGTextureHandle rayDataTex;         // transient
    };
    
    std::vector<DDGIVolumeState> m_Volumes;
    
    // Shader bytecode for DDGI SDK shaders
    std::vector<uint8_t> m_ProbeBlendingIrradianceCS;
    std::vector<uint8_t> m_ProbeBlendingDistanceCS;
    // ... etc.
    
    // Render graph output: indirect lighting texture
    RGTextureHandle m_IndirectOutput;
};
```

**Per-frame flow:** The DDGIRenderer operates in two modes controlled by `m_EnableDDGI`:

```
// LIVE MODE (m_EnableDDGI = true) — probe RT + blending + indirect query
Setup():
  Declare DDGI textures (persistent: irradiance, distance, probe data)
  Declare ray data texture (transient)
  Declare indirect output texture
  Read GBuffer depth/normals
  Write indirect output

Render():
  1. Update() each volume (scroll, random rotation)
  2. Upload constants to GPU
  3. Dispatch probe ray tracing (compute shader with inline `RayQuery` → writes ray data)
  4. UpdateDDGIVolumeProbes() (blend new ray data into persistent textures)
  5. DDGIGetVolumeIrradiance() in fullscreen CS → writes indirect output

// BAKE MODE (m_EnableDDGI = false) — indirect query only, no RT
Setup():
  Declare indirect output texture only
  Read GBuffer depth/normals
  Write indirect output

Render():
  1. DDGIGetVolumeIrradiance() in fullscreen CS → reads baked persistent textures → writes indirect output
```

In bake mode, steps 1-4 are skipped entirely. The persistent irradiance/distance/probe data textures survive across frames (they are long-lived GPU resources). The indirect query shader (`DDGIGetVolumeIrradiance()`) simply samples the pre-converged probe data — it has no dependency on ray tracing. This means:
- **Zero RT cost per frame** in bake mode
- **Zero transient allocations** for ray data
- **TLAS not needed** in bake mode (see §5.1)

### 4.10 Ray Tracing Requirement for DDGI

**Critical note:** DDGI requires GPU ray tracing for probe ray tracing. The probe rays need to trace against the scene's TLAS/BLAS. This means:

- **TLAS is still needed** — but only for DDGI probes, not for shadows
- **BLAS must be built** — they already are (Scene.cpp builds them for meshlet LOD)
- **DXR 1.1 support is required** — DDGI probe rays use inline `RayQuery` in a **compute shader** (no raygen, no any-hit, no hit groups). This keeps everything as a pure `Dispatch()` call with a single compute PSO.

This is a key trade-off: NormalBasic removes ReSTIR DI/GI/SHARC but adds DDGI which still requires RT support. However, DDGI's RT cost is much lower:
- Fewer rays (probeCount × raysPerProbe vs per-pixel rays for ReSTIR)
- Inline RT is simpler and lighter than the DXR hit-group pipeline — no shader tables, no state objects for hit groups
- Can be amortized over multiple frames

### 4.11 DDGI Bake Mode — Use HWRT to Converge, Then Disable RT

The primary use case for DDGI in NormalBasic is **baking** — not real-time updating. The workflow:

```
1. Enable DDGI (m_EnableDDGI = true) → probes ray-trace each frame, converge to steady state
2. Wait for convergence (variability drops below threshold, or manual bake timer)
3. Disable DDGI (m_EnableDDGI = false) → probes stop updating, RT cost drops to zero
4. Enjoy baked GI: indirect query reads the persistent converged probe textures
```

**Feature flag and ImGui control:**

```cpp
// In Renderer.h
bool m_EnableDDGI = false;  // Enable DDGI probe ray tracing + blending (for baking)
```

```cpp
// In ImGuiLayer.cpp — "DDGI" section
if (ImGui::CollapsingHeader("DDGI (Global Illumination)"))
{
    ImGui::Checkbox("Enable DDGI (Probe RT)", &g_Renderer.m_EnableDDGI);
    if (g_Renderer.m_EnableDDGI)
    {
        ImGui::TextColored(ImVec4(1,1,0,1), "Probes converging... (RT active)");
        ImGui::SliderFloat("Convergence Threshold", &g_Renderer.m_DDGIConvergenceThreshold, 0.001f, 0.1f);
        if (ImGui::Button("Stop When Converged"))
            g_Renderer.m_DDGIAutoStop = true;
    }
    else
    {
        ImGui::TextColored(ImVec4(0,1,0,1), "Baked GI (no RT cost)");
    }
}
```

**What stays and what goes:**

| Component | `m_EnableDDGI = true` | `m_EnableDDGI = false` |
|---|---|---|
| Probe ray tracing (inline RT in CS) | ✅ Runs each frame | ❌ Skipped |
| `UpdateDDGIVolumeProbes()` blending | ✅ Blends new rays | ❌ Skipped |
| Irradiance/distance/probe data textures | ✅ Updated (persistent) | ✅ Preserved (persistent, baked) |
| Indirect query (`DDGIGetVolumeIrradiance`) | ✅ Runs | ✅ Runs (reads baked data) |
| TLAS requirement | ✅ Needed | ❌ Not needed |
| RT cost per frame | ~930K rays | **0** |
| GPU memory | ~36 MB (3 volumes) + ray data transient | ~36 MB (3 volumes, persistent only) |

**Key design points:**

- **Persistent textures survive disabling:** The irradiance, distance, and probe data textures are allocated as long-lived GPU resources (not per-frame transient). When `m_EnableDDGI` is toggled off, these textures retain their last converged state — the indirect query shader simply reads from them as before.
- **DDGI SDK state is preserved:** The SDK's `DDGIVolume` objects and their internal state (probe offsets, classifications) remain in host memory. The renderer just stops calling `Update()` and `UpdateDDGIVolumeProbes()`.
- **No hot-reload needed:** Switching between bake and live modes is a single checkbox — no scene reload, no probe data loss. Re-enabling DDGI resumes probe updates from the current converged state.
- **Volumes are static at scene load:** When `m_EnableDDGI` is toggled off, the irradiance, distance, and probe data textures retain their last converged state
- **TLAS is freed in bake mode:** When `m_EnableDDGI = false`, `TLASRenderer` can be skipped entirely (see §5.1). No BLAS rebuild, no TLAS update — pure raster pipeline.
- **Convergence detection:** The SDK provides `CalculateDDGIVolumeVariability()` and `ReadbackDDGIVolumeVariability()` (see §4.9). When variability drops below threshold across all volumes, probes are considered "converged enough." This can be used for automatic bake stop.

**Bake workflow example:**

```
User workflow:
  Load scene → Enable DDGI checkbox → wait 2-5 seconds (probes converge)
  → Variability drops below 0.02 → auto-stop or manual uncheck
  → "Baked GI (no RT cost)" shown in UI
  → Enjoy indirect lighting at zero RT cost for the rest of the session

Developer workflow (for shipping):
  1. Enable DDGI, place camera at key positions
  2. Let probes converge at each position
  3. Serialize converged probe textures to disk
  4. Ship game with baked probe data
  5. At runtime: load baked textures, run indirect query only (m_EnableDDGI = false)


### 4.12 Automatic Scene-Driven Volume Placement

#### 4.13.1 Motivation

Manual volume placement (one giant volume, or 3 ISVs at fixed camera offsets) wastes probes in empty space. A single large volume over Bistro places thousands of probes in walls, the sky, and between disconnected rooms. The goal is to **empirically determine** the optimal set of volumes from the scene geometry itself during load — with zero probes in empty space and maximum coverage of "interesting" areas where indirect lighting matters.

| Scene | Naive 1-giant-volume | Automatic Scene-Driven |
|---|---|---|
| **Sponza** (single open hall) | ✅ Works fine — 1 volume is optimal | 1 volume (same result) |
| **Bistro** (multi-room building + courtyard) | ❌ Thousands of probes in walls/void | 8-15 OBB volumes, each tightly fitting a room/corridor |
| **Cornell Box** (single room) | ✅ Works fine | 1 volume |

#### 4.13.2 Voxel-Based Occupancy Map

**Step 1 — Scene Voxelization:** On scene load, build a coarse 3D occupancy grid over the scene's AABB. Default voxel size: 1.0m (configurable). Each voxel is classified by casting a small number of short rays from its center:

```
Voxel Classification:
  ┌─ All rays miss (hit skybox):          EMPTY_SKY     ← no geometry, outdoor
  ├─ All rays hit geometry immediately:    INSIDE_WALL   ← inside solid geometry
  ├─ Some rays hit, some miss:            SURFACE       ← near geometry — HIGH VALUE
  ├─ No rays hit but not sky:             EMPTY_INDOOR  ← open space indoors — MEDIUM VALUE
  └─ Below scene floor:                   BELOW_FLOOR   ← ignore
```

Only `SURFACE` and `EMPTY_INDOOR` voxels are candidates for probe placement. `INSIDE_WALL`, `EMPTY_SKY`, and `BELOW_FLOOR` are excluded.

**Step 2 — Probe Efficiency Score:** For each candidate voxel, estimate how many of its probes would be useful:

```cpp
float efficiency = (numSurfaceRays + numIndoorRays * 0.3f) / totalRays;
// SURFACE voxels: efficiency ~0.8-1.0 (most probes hit geometry)
// EMPTY_INDOOR voxels: efficiency ~0.3-0.5 (some probes hit walls, some see empty space)
// EMPTY_SKY/INSIDE_WALL: efficiency ~0 (all probes wasted)
```

This creates a 3D scalar field where high values indicate "good places for DDGI probes."

#### 4.13.3 Connected Component Extraction

The efficiency field is thresholded (default: ≥ 0.3) to produce a binary mask. 3D flood-fill extracts connected components — each represents a **spatially contiguous region** that deserves its own DDGI volume.

For Bistro, this naturally yields:
- Each room → one connected component
- Each corridor → one connected component (long and thin)
- Courtyard → one large outdoor component
- Stairwells → one vertical component

#### 4.13.4 OBB Fitting via PCA

For each connected component, fit an Oriented Bounding Box using Principal Component Analysis:

1. Collect all voxel centers in the component as a point cloud
2. Compute the centroid → volume origin
3. Compute the 3×3 covariance matrix → eigenvectors give the OBB axes
4. Project points onto each axis → min/max gives the OBB extent
5. The OBB orientation naturally aligns with room walls (PCA finds the dominant directions)

```cpp
struct CandidateVolume {
    float3 origin;       // centroid of voxel cluster
    float3 eulerAngles;  // from PCA eigenvectors → rotation matrix
    int3   probeCounts;  // extent / probeSpacing, rounded up
    float  efficiency;   // mean voxel efficiency within this volume
    AABB   worldAABB;    // for frustum culling
};
```

#### 4.13.5 Greedy Volume Selection

Connected components may overlap (e.g., a room and its adjacent corridor share boundary voxels). Greedy selection with a coverage map resolves this:

1. Sort candidate volumes by `efficiency × volume` (largest, most efficient first)
2. For each candidate in order:
   - Mark its voxels as "covered" in a global occupancy mask
   - If ≥ 70% of its voxels are already covered by previous volumes, skip it
   - Otherwise, create the volume and mark its voxels as covered
3. Stop when coverage reaches 95% of the efficiency field or max volume count (default: 32)

#### 4.13.6 Post-Processing

After greedy selection, apply cleanup passes:

| Step | Description |
|---|---|
| **Size clamp** | Volumes smaller than 3×3×3 probes are discarded (too small to be useful). Their voxels are left for SSGI to handle. |
| **Merge aligned neighbors** | If two volumes have similar OBB orientation (dot product > 0.9) and their AABBs touch or overlap, merge them into one larger volume. This reduces the number of volumes without sacrificing probe efficiency. |
| **Outdoor expansion** | For `EMPTY_SKY` voxels adjacent to `SURFACE` voxels, expand the nearest volume by 1-2 probe rows to capture outdoor indirect at building edges. |
| **Padding** | Each volume is padded by 1 probe spacing on all sides to ensure probes near volume edges can still interpolate correctly. |

#### 4.13.7 Scene Examples

**Sponza** (single open hall, ~60m × 20m × 25m):
```
Voxelization: 60×20×25 = 30,000 voxels at 1m
Connected components: 1 (the entire interior)
OBB fit: roughly axis-aligned, matching the building orientation
Result: 1 volume, 20×10×12 probes at 3m spacing
```

**Bistro** (multi-room, ~80m × 40m × 15m):
```
Voxelization: 80×40×15 = 48,000 voxels at 1m
Connected components: ~35 (each room, corridor, outdoor area)
After greedy selection + merge: 10-18 volumes
Example volumes:
  - Main dining room: 12×8×6 probes, OBB aligned with room walls
  - Kitchen: 8×6×6 probes, rotated 15° to match wall orientation
  - Outdoor courtyard: 16×12×4 probes, axis-aligned
  - Narrow corridor: 20×3×4 probes (long thin volume)
  - Stairwell: 4×4×8 probes (vertical volume)
Total probes: ~3,000-5,000 (vs ~12,000 for one giant volume)
Probe efficiency: ~70-85% (vs ~30% for one giant volume)
```

#### 4.13.8 Runtime Behavior

These volumes are **static** — positioned once at scene load, never move. Unlike ISVs, they don't follow the camera. This is ideal for the bake workflow:

- Volumes are placed where geometry actually exists
- No probes in walls, sky, or disconnected void
- Probe classification (#4.7) can still deactivate individual probes near dynamic objects
- Bake mode (#4.12) converges these volumes and then disables RT

For moving-camera scenarios, the static volumes can be supplemented with a single camera-following "player bubble" ISV that covers the area immediately around the viewer.

#### 4.13.9 Implementation Outline

```cpp
// In SceneLoader or a new DDGIVolumePlacer class:
struct DDGIVolumePlacer {
    struct Config {
        float voxelSize       = 1.0f;   // meters per voxel
        float probeSpacing    = 2.0f;   // meters between probes
        float efficiencyThresh = 0.3f;  // minimum voxel efficiency
        float coverageTarget  = 0.95f;  // stop when this fraction of voxels is covered
        uint32_t maxVolumes   = 32;     // hard limit
        float minVolumeProbes = 27;     // 3×3×3 minimum
    };

    std::vector<rtxgi::DDGIVolumeDesc> PlaceVolumes(
        const std::vector<MeshInstance>& sceneGeometry,
        const Config& config);

private:
    // Step 1: Voxelize scene, classify each voxel
    VoxelGrid Voxelize(const AABB& sceneBounds, float voxelSize);
    
    // Step 2: Score voxels by probe efficiency
    void ScoreVoxels(VoxelGrid& grid, const TLAS& tlas);
    
    // Step 3: Extract connected components above efficiency threshold
    std::vector<Component> ExtractComponents(const VoxelGrid& grid, float threshold);
    
    // Step 4: Fit OBB to each component via PCA
    CandidateVolume FitOBB(const Component& comp, float probeSpacing);
    
    // Step 5: Greedy coverage-based selection
    std::vector<CandidateVolume> GreedySelect(
        std::vector<CandidateVolume>& candidates,
        const VoxelGrid& grid, float coverageTarget, uint32_t maxVolumes);
    
    // Step 6: Post-process (merge, pad, clamp)
    void PostProcess(std::vector<CandidateVolume>& volumes);
};
```

#### 4.13.10 Design Rationale

| Decision | Choice | Rationale |
|---|---|---|
| Voxel-based vs BVH traversal | Simple 3D voxel grid | Much simpler to implement; BVH traversal is overkill for coarse placement decisions. |
| PCA OBB vs AABB | OBB | Rooms are rarely axis-aligned. PCA OBB naturally aligns with dominant wall directions, reducing wasted probes at diagonal walls. |
| Greedy vs global optimization | Greedy with coverage map | Global optimization (e.g., integer programming) is intractable for 3D volume placement. Greedy with coverage tracking achieves >90% of optimal in practice. |
| Static vs ISV | Static placement | Matches the bake workflow; volumes don't need to move. ISVs are for camera-following runtime scenarios. |
| Ray-cast voxel classification | Use existing TLAS/BLAS | Scene already has RT acceleration structures built at load time. Casting a few rays per voxel is fast (~4 rays × 50K voxels = 200K rays, <1ms on GPU). |
| Outdoor expansion | Extend by 1-2 probe rows | Building exteriors need indirect too. Expanding volumes slightly captures light bounce off facades. |

---

## 5. Feature Dependency Map & Disabled Features

### 5.1 Feature Dependency Map

```
NormalAdvanced (current Normal):
  TLAS ────────┬──→ RTXDIRenderer (ReSTIR DI + GI)
               │       └── NRD (REBLUR denoising)
               │       └── OMM (opacity micromaps)
               ├──→ SHARCRenderer (spatial hash radiance cache)
               │       └── requires TLAS for ray queries
               └──→ DeferredRenderer (HWRT shadows via inline ray queries)   ← REMOVED in NormalBasic

NormalBasic (m_EnableDDGI = true):
  ───→ HWRT shadows REMOVED — replaced by CSM (ShadowRenderer + ShadowMaskRenderer) [prior phase]
  ───→ TLAS still needed for DDGI probe rays only
  ───→ ShadowRenderer (depth-only raster, NO TLAS needed)
  ───→ DDGIRenderer (probe rays NEED TLAS)
  ───→ NO ReSTIR DI, NO ReSTIR GI, NO SHARC, NO NRD, NO OMM, NO TLV
  ───→ TransparentPassRenderer (simplified forward — sun + DDGI indirect only; no TLV)

NormalBasic (m_EnableDDGI = false) — baked GI mode:
  ───→ HWRT shadows via CSM (ShadowRenderer + ShadowMaskRenderer) [prior phase]
  ───→ ShadowRenderer (depth-only raster)
  ───→ DDGIRenderer (indirect query only, NO TLAS)
  ───→ TransparentPassRenderer (simplified forward — sun + DDGI indirect only; no TLV)
  ───→ NO TLAS, NO ReSTIR DI, NO ReSTIR GI, NO SHARC, NO NRD, NO OMM, NO TLV
  ───→ Pure raster pipeline. Zero RT cost. Zero HWRT shadows.
```

### 5.2 What Actually Gets Skipped

| Component | What Happens | Impact |
|---|---|---|
| `TLASRenderer` | **Conditional** — scheduled only when `m_EnableDDGI=true` (DDGI probe ray tracing). Skipped entirely in bake mode (`m_EnableDDGI=false`) — pure raster pipeline, zero RT cost. | TLAS is the _only_ RT dependency remaining in NormalBasic. |
| `RTXDIRenderer` | Entire renderer skipped (`Setup()` returns false when `m_EnableReSTIRDI=false`) | Saves: RIS buffer alloc, presampling, temporal resampling, spatial resampling, compositing, NRD denoising passes |
| `SHARCRenderer` | Entire renderer skipped | Saves: Update, Resolve, Query passes |
| `NrdIntegration` | Not instantiated | Saves: REBLUR denoiser, permanent/transient pools, PackNormalRoughness pass |
| OMM | Not built for BLAS (or built with `AllowOMM=false`) | Saves: OMM build time, OMM memory |
| HWRT Shadows (`m_EnableRTShadows`) | **Already removed in prior phase** — replaced by CSM (ShadowRenderer + ShadowMaskRenderer) | DeferredRenderer reads R8 shadow mask instead of firing inline ray queries |
| TLV (Translucency Lighting Volume) | Entirely skipped — TLV injection reads ReSTIR DI RIS buffers + SHARC resolved buffers, both unavailable | Transparent objects use simplified forward lighting (sun + DDGI indirect only); see §7 |

### 5.3 DDGI-Specific Feature Flags

| Flag | Type | Default | Purpose |
|---|---|---|---|
| `m_EnableDDGI` | `bool` | `false` | When `true`: probe ray tracing (needs TLAS) + blending runs each frame. When `false`: only indirect query runs from persistent baked probe textures (no RT, no TLAS). |

---

## 6. DeferredRenderer Modifications

The `DeferredRenderer` currently has these conditional inputs:
```cpp
// ReSTIR DI composited output
if (g_Renderer.m_EnableReSTIRDI) → reads g_RG_RTXDIDIComposited

// SHARC indirect output
if (g_Renderer.m_IndirectLightingTechnique == SHARC) → reads g_RG_SHARCIndirect
```

For NormalBasic (DDGI phase), modify to:
```cpp
// DI source: shadow mask (precomputed per-pixel visibility) — prior CSM phase
// Indirect source: DDGI indirect output (this phase)
if (g_Renderer.m_Mode == RenderingMode::NormalBasic)
{
    renderGraph.ReadTexture(g_RG_ShadowMask);     // Read precomputed shadow mask (R8_UNORM)
    renderGraph.ReadTexture(g_RG_DDGIIndirect);   // Read DDGI indirect output
}
```

The deferred lighting shader bindings:
```hlsl
Texture2D<float>             g_ShadowMask;    // R8_UNORM, screen resolution — from CSM phase
Texture2D<float4>            g_DDGIIndirect;  // Indirect irradiance — from DDGI phase
```

In `DeferredLighting_PSMain`, the shadow factor and indirect GI are read with single loads:
```hlsl
lightingInputs.sunShadow = g_ShadowMask.Load(uint3(uvInt, 0));
float3 ddgiIndirect = g_DDGIIndirect.Load(uint3(uvInt, 0)).rgb;
```

---

## 7. Transparent Lighting in NormalBasic — No TLV

The [Translucency Lighting Volume (TLV)](implementation_plan_restir_sharc_transparent.md) is an **advanced-only feature** that bakes ReSTIR DI stochastic direct lighting and SHARC indirect GI into a 3D volume grid. It is **excluded from NormalBasic**:

| Dependency | TLV Requirement | NormalBasic Status |
|---|---|---|
| ReSTIR DI RIS buffers | TLV injection pass reads RIS candidate lights | ❌ RTXDIRenderer skipped — no RIS buffers |
| SHARC resolved radiance cache | TLV injection pass queries SHARC for indirect | ❌ SHARCRenderer skipped — no radiance cache |
| RTXDI + SHARC render passes | Must run before TLV injection | ❌ Both disabled |

The transparent forward pass falls back to a simplified path using DDGI:

```hlsl
// NormalBasic transparent forward lighting:
// ── Direct: sun + CSM shadow mask ──
float sunShadow = g_ShadowMask.SampleLevel(linearSampler, screenUV, 0).r;
float3 directDiffuse = EvaluateSunLight(baseColor, normal, sunDirection) * sunShadow;

// ── Indirect: DDGI probe query ──
float3 ddgiIndirect = DDGIGetVolumeIrradiance(worldPos, surfaceBias, normal, volumes);
float3 indirectGI = ddgiIndirect * baseColor * (1.0 - metallic);

float3 color = directDiffuse + indirectGI;
```

| Aspect | NormalAdvanced (TLV) | NormalBasic (DDGI indirect) |
|---|---|---|
| **Direct light** | ReSTIR DI RIS from all scene lights | Single directional sun (analytic) |
| **Direct shadows** | Baked into TLV from stochastic samples | CSM shadow mask (R8 screen-space) — prior phase |
| **Indirect GI** | SHARC 2–4 bounce diffuse GI | DDGI probe-based indirect (baked or live) — this phase |
| **Light count scaling** | O(1) | O(1) — always 1 sun |
| **Per-pixel cost** | 2 trilinear samples (~16 taps) | 1 shadow mask sample + 1 DDGI probe query |

---

## 8. Implementation Roadmap

### Phase 1: Enum & Skeleton (shared — likely already done from CSM phase)

1. Add `RENDERING_MODE_NORMAL_BASIC = 3` to [Common.sr](../src/shaders/Common.sr)
2. Rename `RENDERING_MODE_NORMAL` → `RENDERING_MODE_NORMAL_ADVANCED`
3. Update `RenderingMode` enum in [Renderer.h](../src/Renderer.h)
4. Add `NormalBasic` branch in `ScheduleAndRunAllRenderers()`
5. Update ImGui combo in [ImGuiLayer.cpp](../src/ImGuiLayer.cpp)
6. Set `m_EnableReSTIRDI = false`, `m_EnableRTShadows = false`, `m_IndirectLightingTechnique = 0` in NormalBasic

### Phase 2: CSM Integration (prior phase — already present)

- CSM shadow maps + shadow mask already implemented and wired into `DeferredRenderer`
*(Full details in [CSM_Analysis.md](CSM_Analysis.md))*

### Phase 3: DDGIRenderer (this phase)

1. Integrate RTXGI-DDGI SDK (`rtxgi-sdk/include`, `rtxgi-sdk/shaders`, `rtxgi-sdk/src`)
2. Create `src/DDGIRenderer.h` / `src/DDGIRenderer.cpp`
3. Compile DDGI SDK shaders with `RTXGI_DDGI_RESOURCE_MANAGEMENT=0` (unmanaged), bindless mode
4. Implement automatic scene-driven volume placement (§4.12): voxelize scene, fit OBBs via PCA, greedy selection
5. Implement probe ray tracing (compute shader with inline `RayQuery` using existing TLAS)
6. Call SDK: `UpdateDDGIVolumeProbes()`, `RelocateDDGIVolumeProbes()`, `ClassifyDDGIVolumeProbes()`
7. Fullscreen CS pass calling `DDGIGetVolumeIrradiance()` → writes `g_RG_DDGIIndirect`
8. Wire `g_RG_DDGIIndirect` into `DeferredRenderer`
9. Add `m_EnableDDGI` flag (default `false`) in `Renderer.h` — controls probe RT + blending
10. Implement bake mode: when `m_EnableDDGI = false`, skip probe RT/blending but still run indirect query
11. Conditionally schedule `TLASRenderer`: only when `m_EnableDDGI = true`
12. Add ImGui checkbox for `m_EnableDDGI` + convergence indicators in `ImGuiLayer.cpp`
13. Implement convergence detection: `CalculateDDGIVolumeVariability()` → auto-stop when below threshold

### Phase 4: Integration & Polish

1. Modify `DeferredRenderer` to conditionally use CSM + DDGI in NormalBasic mode
2. Add ImGui controls for DDGI:
   - DDGI enable checkbox + convergence threshold + auto-stop
   - DDGI probe density presets (Low/Medium/High)
   - Variability readout
3. Profile and tune scene-driven volume count, probe density, ray counts per probe
4. Test DDGI bake workflow: enable → wait for convergence → disable → verify zero RT cost + correct indirect
5. Test corner cases: moving camera with baked probes, thin geometry, outdoor scenes, scroll-edge probe updates
6. Verify TLAS is not scheduled in bake mode (`m_EnableDDGI = false`)

---

## 9. DDGI + SSGI Hybrid Blending

### 9.1 Problem Statement

DDGI and SSGI have **complementary strengths and weaknesses**. DDGI provides stable, world-space indirect lighting everywhere but at coarse resolution limited by probe density. SSGI provides high-resolution, per-pixel indirect lighting but is screen-space only (fails at screen edges, disocclusions, and off-screen geometry).

A robust hybrid approach gives **DDGI precedence** (it's always correct, just low-res) and falls back to SSGI where DDGI quality is insufficient. The goal is to **never show DDGI blockiness** when SSGI could produce a crisper result, while **never showing SSGI screen-edge artifacts** when DDGI has valid coverage.

### 9.2 Quality Metrics

Six DDGI metrics and five SSGI metrics determine which technique is more trustworthy at each pixel:

#### 9.2.1 DDGI Quality Metrics

| # | Metric | Source | Range | Interpretation |
|---|--------|--------|-------|----------------|
| 1 | **Aggregate Volume Coverage** | Σ `DDGIGetVolumeBlendWeight(worldPos, volume_i)` across all volumes | [0, N] | DDGI samples irradiance from every volume with non-zero blend weight and accumulates the result weighted by each volume's contribution (see reference sample `IndirectCS.hlsl`). With scene-driven volumes, most of the visible scene is covered by at least one volume. Only when ALL volumes return zero weight does DDGI truly have no data → SSGI must take over. |
| 2 | **Probe Resolution Ratio** | `probeSpacing / pixelFootprintAt(worldPos)` | (0, ∞) | How many screen pixels fit between two adjacent probes. > 4 means each probe covers ≥4×4 pixels → DDGI is visibly coarse → SSGI preferred for detail. |
| 3 | **Distance to Nearest Probe** | `min(|worldPos - probeWorldPos_i|) / probeSpacing` | [0, √3] | Normalized distance within the probe voxel. 0 = exactly at a probe; ~0.87 = at voxel corner. Higher values mean worse trilinear interpolation quality. |
| 4 | **DDGI Irradiance Spatial Gradient** | `|∇irradiance|` between adjacent probe texels | (0, ∞) | High gradient means sharp lighting change that DDGI's bilinear/trilinear interpolation cannot capture smoothly → SSGI can resolve it better. |
| 5 | **DDGI Distance Mean/Variance** | From probe distance texture | (0, ∞) | High variance in probe distance suggests complex geometry near the probes that the coarse grid can not represent well. |
| 6 | **Temporal Latency** | Probe frame-age (row slicing) | {1, 2, 4} | Near probes updated every 2nd frame; far probes every 4th. In fast-moving scenes, stale DDGI data may lag behind SSGI which is per-frame. |

#### 9.2.2 SSGI Quality Metrics

| # | Metric | Source | Range | Interpretation |
|---|--------|--------|-------|----------------|
| 7 | **Temporal Age (Convergence)** | `ssgiAge = 1/(1-blend)-1` from accum alpha | [0, ~9] | 0 = brand new (noisy); 9 = fully converged (~9 frames). Young pixels are unreliable → prefer DDGI. |
| 8 | **Screen-Edge Distance** | `min(x, y, width-x, height-y) / max(width,height)` | [0, 0.5] | Distance from nearest screen edge in NDC. SSGI data doesn't exist outside the screen — pixels near edges may sample invalid data. |
| 9 | **Ray March Hit Rate** | Hit mask from SSGI pass 1 | {0, 1} | 0 = ray march found no surface in the allowed distance → SSGI has no data → DDGI must take over. |
| 10 | **Disocclusion Detection** | `|prevDepth - reprojectedDepth| > threshold` | {0, 1} | Newly visible pixels have no temporal history → noisy SSGI → prefer DDGI. |
| 11 | **Depth Complexity** | Local depth gradient magnitude | (0, ∞) | High depth variation means thin/detailed geometry. SSGI ray march may miss thin features → DDGI more reliable. |

### 9.3 Recommended Blending Strategy

#### 9.3.1 Primary: Per-Pixel Confidence Cascade

```hlsl
// ── Step 1: Compute DDGI confidence (0 = untrustworthy, 1 = perfect) ──

// Aggregate volume coverage: accumulate blend weights across all volumes
float ddgiAggregateCoverage = 0.0;
for (int vi = 0; vi < RTXGI_DDGI_NUM_VOLUMES; vi++)
    ddgiAggregateCoverage += DDGIGetVolumeBlendWeight(worldPos, volumes[vi]);
float ddgiVolWeight = saturate(ddgiAggregateCoverage); // 0 only when outside ALL volumes

// Probe resolution: how many screen pixels span one probe cell?
float pixelFootprint = length(ddx(worldPos)) + length(ddy(worldPos));
float probeResRatio = probeSpacing / max(pixelFootprint, 0.001);
float ddgiResQuality = 1.0 - saturate((probeResRatio - 1.0) / 3.0); // 0 when >4px/probe

// Distance to nearest probe center (normalized to probe spacing)
float3 probeLocal = (worldPos - volumeOrigin) / probeSpacing;
float3 probeDist = abs(frac(probeLocal) - 0.5); // [0, 0.5]
float ddgiProbeDist = 1.0 - saturate(length(probeDist) / 0.7); // 1 at center, 0 at corner

// DDGI confidence: dominated by volume weight
float ddgiConfidence = ddgiVolWeight * lerp(0.4, 1.0, ddgiResQuality * ddgiProbeDist);
// Note: even when volWeight=0 (outside), ddgiConfidence=0 → SSGI takes over fully


// ── Step 2: Compute SSGI confidence ──

// Temporal age: how converged is the SSGI sample?
float ssgiAge = g_AccumDiffuse.Load(uint3(uvInt, 0)).a;
float ssgiAgeConf = saturate(ssgiAge / 5.0); // linearly rises to full confidence after ~5 frames

// Screen-edge falloff
float2 screenEdgeDist = min(uv, 1.0 - uv);
float ssgiEdgeConf = smoothstep(0.0, 0.05, min(screenEdgeDist.x, screenEdgeDist.y));

// Hit success: did the SSGI ray march find geometry?
float ssgiHitConf = float(g_RawDiffuse.Load(uint3(uvInt, 0)).r >= 0.0); // sentinel = -1 on miss

// Disocclusion
float depthDiff = abs(g_Depth.Load(uint3(uvInt, 0)) - reprojectedDepth);
float ssgiDisoccConf = 1.0 - saturate(depthDiff / 0.1);

// SSGI confidence
float ssgiConfidence = ssgiAgeConf * ssgiEdgeConf * ssgiHitConf * ssgiDisoccConf;


// ── Step 3: Blend ──

// DDGI takes precedence; SSGI fills where DDGI is weak AND SSGI is confident
float ddgiWeight = ddgiConfidence;
float ssgiWeight = (1.0 - ddgiConfidence) * ssgiConfidence;

// Normalize so total weight ≤ 1 (prevent double-lighting at overlap)
float totalWeight = ddgiWeight + ssgiWeight;
if (totalWeight > 0.001)
{
    ddgiWeight  /= totalWeight;
    ssgiWeight  /= totalWeight;
}

float3 indirectGI = ddgiIndirect * ddgiWeight + ssgiIndirect * ssgiWeight;
```

#### 9.3.2 Secondary: Spatial Variance-Based Demotion

As an additional safety check, detect when DDGI produces **visibly incorrect** results and demote those pixels to SSGI:

```hlsl
// Detect DDGI blockiness: compute local irradiance gradient
float3 ddgiCenter = g_DDGIIndirect.Load(uint3(uvInt, 0)).rgb;
float3 ddgiRight  = g_DDGIIndirect.Load(uint3(uvInt + int2(1,0), 0)).rgb;
float3 ddgiDown   = g_DDGIIndirect.Load(uint3(uvInt + int2(0,1), 0)).rgb;
float ddgiGradient = length(ddgiRight - ddgiCenter) + length(ddgiDown - ddgiCenter);

// High gradient + coarse probe resolution = DDGI is undersampled
float ddgiBlockiness = saturate(ddgiGradient * probeResRatio / 0.05);

// Demote DDGI weight when blocky
if (ddgiBlockiness > 0.5 && ssgiHitConf > 0.5)
    ddgiWeight *= (1.0 - ddgiBlockiness);
```

### 9.4 Artifact Comparison

| Artifact | DDGI-Only | SSGI-Only | Hybrid |
|---|---|---|---|
| **Blocky/coarse indirect** | ✅ Visible | ❌ Not present | ✅ Fixed: SSGI fills where DDGI is coarse |
| **Screen-edge missing GI** | ❌ DDGI covers everywhere | ✅ Visible | ✅ Fixed: DDGI fills where SSGI can't see |
| **Disocclusion noise** | ❌ No temporal dependency | ✅ Visible | ✅ Fixed: DDGI covers disoccluded regions |
| **Light leaking through walls** | ✅ Visible with thin geometry | ❌ Handled by ray march | ✅ Fixed: SSGI takes over near thin geometry |
| **Temporal lag on moving camera** | ✅ With static volumes | ❌ Per-frame | ✅ Mitigated: SSGI fills during DDGI latency |
| **Outside-volume ambient** | ✅ Missing | ❌ SSGI works on-screen | ✅ Fixed: SSGI fills outside volumes |
| **Ray-march misses (sky)** | ❌ DDGI has data | ✅ No SSGI data | ✅ Fixed: DDGI covers missed rays |

### 9.5 Render Pass Integration

```
Render Pass Order (NormalBasic with DDGI+SSGI):

  ShadowRenderer → ShadowMaskRenderer
      │
      ▼
  ┌─────────────────────────────────────┐
  │ DDGIRenderer                        │
  │  → g_RG_DDGIIndirect (persistent)   │
  └─────────────────────────────────────┘
      │
      ▼
  ┌─────────────────────────────────────┐
  │ SSGIRenderer (modified)             │
  │  → Reads g_RG_DDGIIndirect          │
  │  → Per-pixel blend DDGI+SSGI        │
  │  → g_RG_SSGIComposed (hybrid output)│
  └─────────────────────────────────────┘
      │
      ▼
  DeferredRenderer → reads g_RG_SSGIComposed
```

**Key design choice:** The DDGI+SSGI blend happens **inside SSGIRenderer** (specifically, in the SSGI Compose pass, `SSGICompose.hlsl`). This keeps DDGIRenderer focused on probe management and makes SSGIRenderer the single indirect-lighting output. `DeferredRenderer` continues to consume `g_RG_SSGIComposed` unchanged.

**Modifications to SSGIRenderer:**
- `Setup()`: declare `g_RG_DDGIIndirect` as an input read texture (available when DDGI is enabled)
- `SSGICompose.hlsl`: after the existing BRDF compose logic, blend with DDGI indirect using the confidence cascade above
- When DDGI is not enabled, `g_RG_DDGIIndirect` resolves to a 1×1 black texture (render graph default) → the blend degenerates to SSGI-only, preserving backward compatibility

### 9.6 Tuning Parameters

| Parameter | Default | Effect |
|---|---|---|
| `DDGI_SSGI_BlendSharpness` | 1.0 | Steepness of the DDGI→SSGI transition. Higher = sharper cutoff. |
| `DDGI_SSGI_ResThreshold` | 4.0 | probeResRatio above which DDGI is considered "coarse" (in screen px/probe). |
| `DDGI_SSGI_EdgeMargin` | 0.05 | Screen-edge margin as fraction of NDC for SSGI confidence ramp. |
| `DDGI_SSGI_AgeConverge` | 5.0 | Number of frames for SSGI temporal age to reach full confidence. |
| `DDGI_SSGI_BlockinessDemote` | 0.5 | DDGI spatial gradient threshold to trigger SSGI demotion. |

### 9.7 Design Rationale

| Decision | Choice | Rationale |
|---|---|---|
| Who owns the blend? | SSGIRenderer (Compose pass) | SSGI already produces `g_RG_SSGIComposed`; adding DDGI as an input keeps DeferredRenderer unchanged. |
| DDGI or SSGI first? | DDGI takes precedence | DDGI is always physically grounded (world-space); SSGI is an approximation. DDGI may be coarse but it's never wrong. |
| Outside-volume behavior | SSGI only | `DDGIGetVolumeBlendWeight()=0` across ALL volumes → ddgiConfidence=0 → pure SSGI. With scene-driven volumes this is rare. |
| Screen-edge behavior | DDGI only | SSGI confidence drops to 0 near screen edges → pure DDGI. DDGI has no screen-edge problem. |
| Disocclusion handling | DDGI during disocclusion | SSGI temporal age is 0 → ssgiAgeConf = 0 → pure DDGI. |
| Thin geometry | SSGI preferred | High depth gradient → lower DDGI confidence via probeResRatio → SSGI takes over. |
| Performance cost | ~1 extra texture sample | The blend adds one `g_DDGIIndirect.Load()` + ~20 ALU ops. No extra passes. |

### 9.8 Tile-Based SSGI Dispatch

#### 9.8.1 Motivation

Running SSGI at full screen is wasteful when DDGI covers most pixels with adequate quality. A tile-based approach classifies the screen into **16×16 pixel tiles** and only dispatches SSGI work on tiles where DDGI confidence is insufficient for any pixel. In typical interior scenes with good probe coverage, this can skip **60-90% of SSGI work** — a massive performance win since SSGI's ray-march pass is the most expensive part of the pipeline.

#### 9.8.2 Classification Pass

A lightweight compute shader (one thread per tile) samples a sparse set of representative pixels within each tile and computes DDGI confidence for each. If ALL sampled pixels exceed a conservative threshold, the tile is classified as "DDGI-sufficient" and SSGI is skipped for that tile.

```hlsl
// Tile Classification CS — 1 thread per 16×16 tile
[numthreads(1, 1, 1)]
void TileClassifyCS(uint3 GroupID : SV_GroupID)
{
    uint2 tileCoord = GroupID.xy;
    uint2 tileOrigin = tileCoord * TILE_SIZE;

    // Sparse sample: check 4 corners + center of the tile
    uint2 sampleOffsets[5] = {
        uint2(0, 0), uint2(TILE_SIZE-1, 0), uint2(0, TILE_SIZE-1),
        uint2(TILE_SIZE-1, TILE_SIZE-1), uint2(TILE_SIZE/2, TILE_SIZE/2)
    };

    bool bTileNeedsSSGI = false;
    for (int i = 0; i < 5; i++)
    {
        uint2 pixel = tileOrigin + sampleOffsets[i];

        // Compute DDGI confidence at this pixel (metrics from §9.2.1)
        float ddgiConf = ComputeDDGIConfidence(pixel);
        float ssgiConf = ComputeSSGIConfidence(pixel);

        // Tile needs SSGI if DDGI confidence is below threshold
        // AND SSGI could actually contribute (not at screen edge, not disoccluded)
        if (ddgiConf < g_TileConfidenceThreshold && ssgiConf > 0.1)
        {
            bTileNeedsSSGI = true;
            break;
        }
    }

    // Write tile mask (1 bit per tile, packed as uint)
    if (bTileNeedsSSGI)
        InterlockedOr(g_TileMask[tileCoord.y * g_TileCountX + tileCoord.x / 32],
                      1u << (tileCoord.x % 32));

    // Atomic increment for indirect dispatch count
    uint tileIndex;
    if (bTileNeedsSSGI)
        InterlockedAdd(g_TileIndirectArgs[0], 1, tileIndex);
}
```

#### 9.8.3 Per-Tile Early-Out in SSGI Passes

Each SSGI pass reads the tile mask. Thread groups for DDGI-sufficient tiles immediately return, avoiding all ray-march, temporal reproject, denoise, and compose work for those tiles:

```hlsl
// At the top of every SSGI pass (RayMarch, TemporalReproject, Denoise, Compose):
[numthreads(THREADS_X, THREADS_Y, 1)]
void SSGIPass(uint3 GroupID : SV_GroupID, uint3 GroupThreadID : SV_GroupThreadID)
{
    uint2 tileCoord = GroupID.xy;

    // Check tile mask — skip if DDGI handles this tile entirely
    uint maskWord = g_TileMask[tileCoord.y * g_TileCountX + tileCoord.x / 32];
    uint tileBit  = 1u << (tileCoord.x % 32);
    if ((maskWord & tileBit) == 0)
        return; // entire thread group early-exits

    // ... normal SSGI per-pixel work ...
}
```

This is simpler than true `ExecuteIndirect` dispatch — the cost of launching idle thread groups is negligible on modern GPUs (~0.01ms for a few hundred skipped groups).

#### 9.8.4 Temporal Reprojection Across Tile Boundaries

When a tile transitions from SSGI-active to SSGI-skipped, the SSGI accumulation buffers retain their last computed values. On re-entry (tile becomes active again), the temporal age is stale. Two strategies:

| Strategy | Description | Trade-off |
|---|---|---|
| **Age Reset on Re-entry** | When a tile transitions from skipped → active, reset temporal age to 0 for that tile. The SSGI temporal reproject pass detects `age < 1` and uses only the current frame's raw sample (no history blend). | Simple; causes a 2-3 frame noise burst on re-entry. Acceptable since tile transitions are rare (camera movement, light changes). |
| **Always Run Temporal Reproject** | Run the temporal reprojection pass at full screen (cheap pass, ~0.1ms). Only skip the ray-march and denoise passes. | Eliminates re-entry artifacts at the cost of always running one extra pass. Recommended for quality. |

**Recommended:** Always run temporal reproject at full screen. Its cost is negligible compared to ray-march, and it eliminates all temporal artifacts at tile boundaries.

#### 9.8.5 Tile Boundary Blending

To avoid visible seams between DDGI-only and DDGI+SSGI tiles, the per-pixel confidence cascade from §9.3 naturally feathers across tile boundaries — the DDGI confidence is computed **per-pixel**, not per-tile. The tile mask only controls **dispatch granularity**, not the blend weights themselves.

For additional smoothness, expand the tile mask by 1 tile in each direction (conservative dilation):

```hlsl
// Post-process the tile mask: dilate by 1 tile to feather boundaries
// For each marked tile, also mark its 4-connected neighbors
// This adds ~1 row/column of tiles at the boundary of DDGI/SSGI regions
```

This dilation costs ~4 extra tiles at each boundary, effectively negligible.

#### 9.8.6 Tile Size Selection

| Tile Size | Tiles at 1080p | Classification Cost | SSGI Skip Granularity |
|---|---|---|---|
| 8×8 | 16,200 | ~253 thread groups | Very fine — only skip where DDGI is truly confident |
| 16×16 | 4,050 | ~64 thread groups | Good balance — matches typical probe cell size at 1.5m spacing |
| 32×32 | 1,012 | ~16 thread groups | Coarse — may leave visible blocks of DDGI-only at boundaries |

**Recommended: 16×16 tiles.** At 1080p this produces ~4K tiles. The classification pass dispatches ~64 thread groups (1 thread per tile), which completes in <0.01ms. A 16×16 tile with typical 1.5m probe spacing covers roughly the same world-space area as 2-3 probe cells — a natural granularity for the DDGI quality decision.

#### 9.8.7 Confidence Threshold Design

The tile classification threshold must be **conservative** — it's better to run SSGI on a tile that didn't strictly need it than to skip SSGI on a tile where DDGI is borderline:

| Threshold | Behavior |
|---|---|
| `ddgiConfidence ≥ 0.95` (very conservative) | Only skip SSGI where DDGI is near-perfect. Still skips ~50% of tiles in well-covered interiors. |
| `ddgiConfidence ≥ 0.80` (balanced) | Skip SSGI where DDGI is clearly good enough. Skips ~70% of tiles. Rare visible boundary. |
| `ddgiConfidence ≥ 0.60` (aggressive) | Skip SSGI wherever DDGI is "probably fine." Skips ~85% of tiles. May show occasional blockiness at boundaries. |

**Recommended: 0.80 threshold** with 1-tile conservative dilation. This provides a strong performance win while eliminating virtually all visible tile-boundary artifacts.

#### 9.8.8 Expected Performance Impact

For a typical interior scene at 1080p with scene-driven volumes, assuming ~70% of screen pixels are well-covered by probes:

| Metric | Full-Screen SSGI | Tile-Based SSGI |
|---|---|---|
| Ray march cost | 100% | ~30% (only DDGI-weak tiles) |
| Denoise cost | 100% | ~30% |
| Compose cost | 100% | 100% (always full-screen for smooth output) |
| Temporal reproject cost | 100% | 100% (always full-screen, §9.8.4) |
| Classification cost | — | <0.01ms |
| Total SSGI cost | 100% | ~40-50% |

The compose pass should remain full-screen to ensure every pixel has a valid `g_RG_SSGIComposed` value for `DeferredRenderer`. For DDGI-only tiles, the compose pass outputs black → DeferredRenderer uses only DDGI indirect via the confidence cascade.

#### 9.8.9 Implementation Steps

1. Add `TileClassificationCS.hlsl` compute shader and register in `shaders.cfg`
2. In `SSGIRenderer::Setup()`: declare `g_TileMask` (RWStructuredBuffer, uint per tile row) and `g_TileIndirectArgs` (RWStructuredBuffer, indirect dispatch args)
3. In `SSGIRenderer::Render()`: dispatch tile classification pass, then barrier, then dispatch SSGI passes with per-tile early-out
4. Add `g_TileConfidenceThreshold` to `SSGIRenderer` tuning params (default 0.80)
5. Add `g_TileSize` constant (default 16)
6. Modify `SSGI.hlsl`, `SSGIDenoise.hlsl`: add tile-mask early-out at the top of each entry point
7. Keep `SSGITemporalReproject.hlsl` and `SSGICompose.hlsl` at full screen

#### 9.8.10 Design Rationale

| Decision | Choice | Rationale |
|---|---|---|
| Tile early-out vs ExecuteIndirect | Per-tile early-out in shader | Much simpler to implement; idle thread group cost is negligible. Avoids D3D12 indirect dispatch complexity. |
| Temporal reproject: full-screen | Always run at full screen | Eliminates tile-boundary temporal artifacts. Cost is negligible (~0.1ms). |
| Compose: full-screen | Always run at full screen | Ensures every pixel has a valid SSGI output for DeferredRenderer. DDGI-only pixels get black → DeferredRenderer uses DDGI via confidence cascade. |
| Classification sample count | 5 samples (4 corners + center) | Covers the tile adequately. 5 texture loads × 4K tiles = 20K loads, negligible. |
| Conservative dilation | 1-tile halo | Feathers DDGI/SSGI boundaries at the cost of ~1 row/column of extra tiles. Virtually free. |

---

## Appendix A: File Index

| File | Purpose |
|---|---|
| [Renderer.h](../src/Renderer.h) | `IRenderer` base, `RenderingMode` enum, `Renderer` struct with `m_EnableDDGI` flag |
| [Renderer.cpp](../src/Renderer.cpp) | `ScheduleAndRunAllRenderers()` — pass scheduling with conditional TLAS |
| [Common.sr](../src/shaders/Common.sr) | `CommonConsts` HLSL constants including RENDERING_MODE_* |
| [DeferredRenderer.cpp](../src/DeferredRenderer.cpp) | Deferred lighting pass — conditionally reads DDGI output |
| [DeferredLighting.hlsl](../src/shaders/DeferredLighting.hlsl) | Lighting shader — reads DDGI indirect irradiance |
| [DeferredLighting.sr](../src/shaders/DeferredLighting.sr) | Resource bindings for deferred lighting |
| [BasePass.hlsl](../src/shaders/BasePass.hlsl) | Transparent forward shader — DDGI probe query for indirect |
| [RTXDIRenderer.cpp](../src/RTXDIRenderer.cpp) | ReSTIR DI + GI — to be skipped in NormalBasic |
| [SHARCRenderer.cpp](../src/SHARCRenderer.cpp) | SHARC indirect — to be skipped in NormalBasic |
| [TLASRenderer.cpp](../src/TLASRenderer.cpp) | TLAS build — conditionally scheduled (only when `m_EnableDDGI=true`) |
| [NrdIntegration.h](../src/NrdIntegration.h) | NRD denoiser — to be skipped in NormalBasic |
| [ImGuiLayer.cpp](../src/ImGuiLayer.cpp) | UI controls — RenderingMode combo + DDGI enable checkbox |
| [DDGIVolume.h](../REFERENCES/RTXGI-DDGI/rtxgi-sdk/include/rtxgi/ddgi/DDGIVolume.h) | DDGI volume API |
| [Integration.md](../REFERENCES/RTXGI-DDGI/docs/Integration.md) | DDGI integration guide |
| [DDGIVolume.md](../REFERENCES/RTXGI-DDGI/docs/DDGIVolume.md) | DDGI volume reference |
| [DDGI_D3D12.cpp](../REFERENCES/RTXGI-DDGI/samples/test-harness/src/graphics/DDGI_D3D12.cpp) | DDGI test harness reference implementation |
| [IndirectCS.hlsl](../REFERENCES/RTXGI-DDGI/samples/test-harness/shaders/IndirectCS.hlsl) | Reference indirect query shader |

## Appendix B: Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Shadow technique | CSM (4 cascades) — prior phase | Well-understood, no RT hardware needed for shadows |
| DDGI volume count | Scene-driven (automatic from geometry) | Minimal probes in empty space; volumes fit scene topology |
| DDGI near probe density | 20×12×20, 1.5m spacing | High-quality indirect for surfaces close to camera |
| DDGI medium probe density | 20×10×20, 4.0m spacing | Balanced mid-range coverage |
| DDGI far probe density | 20×10×20, 12.0m spacing | Low-frequency ambient for distant geometry |
| DDGI update strategy | Row slicing: near÷2, med÷2, far÷4 | All volumes updated every frame, consistent ray count, no bursts |
| DDGI volume culling | Frustum culling only | Simple, sufficient; skip volumes fully outside frustum |
| DDGI + far distance | Blend with sky ambient | No complex falloff needed |
| Indirect specular | Not in scope | SSR can be added later if needed |
| DDGI operation mode | `m_EnableDDGI` flag: bake (default off) vs live (on) | Primary use case is baking: converge probes with HWRT, then disable RT; indirect query always runs from persistent textures |
| TLAS | Only needed when `m_EnableDDGI = true` | DDGI probe rays need TLAS; when disabled (bake mode), TLASRenderer is skipped entirely |
| DDGI bake convergence | SDK variability readback + auto-stop | Probes converge in 2-5 seconds; variability < 0.02 indicates "done" |
| Transparent lighting | Simplified forward (sun + DDGI indirect, no TLV) | TLV depends on ReSTIR + SHARC which are disabled |
```
