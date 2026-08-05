# DDGI Implementation Plan

> For AI agent. Granular phases. Each phase = visual verification possible via debug modes.  
> Debug modes are spread across phases — each phase adds the debug visualization for data it produces.  
> Source: `docs/DDGI_Analysis.md` §4.13 (Debug Modes) + §8 (Roadmap).

---

## Debug Mode Enum

Defined in `src/shaders/Common.sr` as `srinput DDGIDebugMode` (auto-generates C++ `srrhi::DDGIDebugMode::` and HLSL `srrhi::DDGIDebugMode::` constants).
Stored as `uint32_t m_DDGIDebugMode` on the `Renderer` struct.

| Value | Constant | Phase | Description |
|---|---|---|---|
| 0 | `DDGI_DEBUG_OFF` | — | Normal rendering |
| 1 | `DDGI_DEBUG_VOLUME_WIREFRAME` | Phase 2 ✅ | OBB wireframes (ImDrawList) |
| 2 | `DDGI_DEBUG_PROBE_POSITIONS` | Phase 3 ✅ | Probe sphere overlay (RT, via ProbeVisCS) |
| 3 | `DDGI_DEBUG_PROBE_IRRADIANCE` | Phase 4.5 | Spherical irradiance look-up on probe spheres |
| 4 | `DDGI_DEBUG_PROBE_DISTANCE` | Phase 4.5 | Distance heatmap on probe spheres |
| 5 | `DDGI_DEBUG_PROBE_CLASSIFICATION` | Phase 4.5 | State colours on probe spheres |
| 6 | `DDGI_DEBUG_INDIRECT_ONLY` | Phase 5 | Raw g_RG_DDGIIndirect output |
| 7 | `DDGI_DEBUG_CONVERGENCE_STATUS` | Phase 3+ ✅ | ImGui progress bars |
| 8 | `DDGI_DEBUG_DDGI_ONLY` | Phase 7 | Final composed DDGI only |
| 9 | `DDGI_DEBUG_SSGI_ONLY` | Phase 7 | Final composed SSGI only |
| 10 | `DDGI_DEBUG_CONFIDENCE_HEATMAP` | Phase 7 | Red=low, green=high |
| 11 | `DDGI_DEBUG_VOLUME_BLEND_WEIGHT` | Phase 7 | Cyan heatmap |
| 12 | `DDGI_DEBUG_TILE_ACTIVITY` | Phase 8 | SSGI dispatch mask |

---

## Phase 1 — DDGIRenderer skeleton ✅

- [x] Create `src/DDGIRenderer.cpp`
- [x] Inherit `IRenderer`, override `Setup(RenderGraph&)`, `Render()`, `GetName()` → `"DDGIRenderer"` (no-op stubs for now)
- [x] `REGISTER_RENDERER(DDGIRenderer)`
- [x] Add `m_EnableDDGIProbeTracing` (bool, default `false`) to `Renderer` struct in `Renderer.h`
- [x] Schedule `DDGIRenderer` after `SSGIRenderer`, before `DeferredRenderer` in `Renderer::ScheduleAndRunAllRenderers()` NormalBasic branch
- [x] `Setup()` returns `false` when `!m_EnableDDGIProbeTracing`
- [x] **Verify:** ImGui checkbox toggles renderer on/off. Checkbox in Indirect Lighting tree node (DDGI+SSGI section), disabled for other indirect lighting modes.

---

## Phase 2 — 1 hardcoded volume + Volume Wireframe debug ✅

- [x] `PostSceneLoad()`: populate `Scene::m_DDGIVolume` — 20×10×20m, 1.5m spacing, 14×8×14 probes, centered at (0,5,0)
- [x] Allocate 3 persistent `Texture2DArray` textures via nvrhi: Irradiance (R10G10B10A2_UNORM), Distance (RG16_FLOAT), ProbeData (RGBA16_FLOAT)
- [x] Register textures in global bindless heap; stored as DDGIRenderer private members (not RG-declared)
- [x] **Deferred to Phase 4:** `rtxgi::d3d12::DDGIVolume::Create()` — requires SDK PSOs, descriptor heaps, root signature
- [x] **Debug — VOLUME_WIREFRAME (mode 1):** `ImDrawList` OBB wireframe in `ImGuiLayer.cpp` — CPU-side Rodrigues rotation + DirectXMath world→screen projection, 12 green line segments via `ImGui::GetForegroundDrawList()`
- [x] `m_DDGIDebugMode` (`uint32_t`) on `Renderer` struct; ImGui combo in Indirect Lighting section ("Off" / "Volume Wireframe")
- [x] **Verify:** Green wireframe box visible on screen at the hardcoded AABB position. No crash, no GPU validation errors.

---

## Phase 2.5 — `rtxgi::DDGIVolumeBase` subclass ✅

Phase 2 already has the volume descriptor (`Scene::m_DDGIVolume`) and nvrhi textures.
This short phase creates a class inheriting `rtxgi::DDGIVolumeBase` that ties the CPU-side
volume descriptor to our GPU textures — using only nvrhi, no raw D3D12/Vulkan.

`DDGIVolumeBase` provides all the CPU utility: `Update()` (rotation matrices), random numbers,
`GetDescGPU()` / `GetDescGPUPacked()`, setters/getters.  The only required override is `Destroy()`.
All GPU resources (textures, PSOs, descriptor heaps) are managed by nvrhi as usual.

The class is defined directly in `src/Scene.h` — **no separate DDGIVolumeNvrhi.h/.cpp files**.
Ownership lives on `Scene` as a `std::vector<DDGIVolumeNvrhi>` (currently 1 element; multiple later).

- [x] Define class `DDGIVolumeNvrhi : public rtxgi::DDGIVolumeBase` **in `src/Scene.h`** (above `class Scene`)
- [x] `DDGIVolumeNvrhi() = default` (default-constructible for `std::vector`)
- [x] `InitFromDesc(const rtxgi::DDGIVolumeDesc&)`: copies the desc into the base class `m_desc`
- [x] Store nvrhi texture handles: `m_IrradianceTexture`, `m_DistanceTexture`, `m_ProbeDataTexture`
- [x] `UploadConstants(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList) -> nvrhi::BufferHandle`:
      - Create volatile CB: `nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(DDGIVolumeDescGPUPacked), "DDGIVolumeCB", 1)`
      - Pack desc via `GetDescGPUPacked()`, write via `commandList->writeBuffer(cb, &packed, sizeof(packed), 0)`
      - Return the CB handle for the caller to bind via `srrhi::*Inputs`
- [x] Override `Destroy()`: release nvrhi texture handles (set to `nullptr`)
- [x] Override `GetGPUMemoryUsedInBytes()`: sum texture + buffer sizes from nvrhi descs
- [x] Store `std::vector<DDGIVolumeNvrhi> m_DDGIVolumes` on `Scene` (replaces `rtxgi::DDGIVolumeDesc m_DDGIVolume`)
- [x] DDGIRenderer `PostSceneLoad()`: populate 1 `DDGIVolumeNvrhi`, create textures, set handles on volume, `push_back` into `Scene::m_DDGIVolumes`
- [x] Call `m_DDGIVolumes[0].Update()` each frame in `Render()` before dispatching probe traces (Phase 3+)
- [x] **Verify:** Volume object constructed. `Update()` succeeds. `GetDescGPUPacked()` returns valid packed data. Log GPU memory.

---

## Phase 3 — Probe trace CS + Probe Position debug + Convergence Status ✅

- [x] Flesh out `src/shaders/ddgi/ProbeTraceCS.hlsl` (189 lines):
  - Thread → (probeIndex, rayIndex) decomposition via `globalIdx / numRays` and `globalIdx % numRays`
  - `DDGIGetProbeCoords()` → probe grid coords, `DDGIGetScrollingProbeIndex()` → scroll-adjusted index
  - Skip inactive probes beyond fixed rays (`probeState == INACTIVE && rayIndex >= NUM_FIXED_RAYS`)
  - `DDGIGetProbeWorldPosition()` with relocation offset
  - `DDGIGetProbeRayDirection(rayIndex, volume)` → Fibonacci sphere sample
  - Inline RT via `RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>` against TLAS
  - Alpha-tested materials: read interpolated UV, `AlphaTest()`, `CommitNonOpaqueTriangleHit()`
  - Miss: `GetAtmosphereSkyRadiance()` → `DDGIStoreProbeRayMiss()`
  - Back-face hit: `DDGIStoreProbeRayBackfaceHit()` with negative hitT
  - Fixed-ray front-face (relocation/classification enabled): `DDGIStoreProbeRayFrontfaceHit()` — hitT only
  - Front-face hit: `GetFullHitAttributes()` → `GetPBRAttributes()` → diffuse lighting (sun only, `TraceSunShadow()`) + emissive
  - Energy clamp: `min(radiance, kMaxAlbedo)` at 0.9
  - `DDGIStoreProbeRayFrontfaceHit()` with radiance + hitT → RayData UAV
- [x] `ProbeTrace.sr` / `ProbeTraceInputs`: full binding set — CB, DDGIVolumes, TLAS, Lights, Instances, MeshData, Materials, Indices, Vertices, ProbeData (read), RayData (read-write)
- [x] Compute PSO for `ProbeTraceCS` registered in `shaders.cfg` → generates `ShaderID::DDGI_PROBETRACECS_PROBETRACECS`
- [x] In `Render()`: `DDGIVolume::Update()` → upload packed GPU descriptor → dispatch `ceil(totalRays / 64)` groups
- [x] **Debug — PROBE_POSITIONS (mode 2):**
  - `src/shaders/ddgi/ProbeVisCS.hlsl` (added in Phase 4): RT-based sphere visualization via RayQuery — traces probe TLAS instances, decodes instance ID → volume/probe index, projects sphere position, colours by debug mode
  - (Obsolete) `ProbeOverlayCS.hlsl` was the original per-probe dot overlay; superseded by ProbeVisCS in Phase 4, deleted
  - `DDGIRenderer::Setup()`: always declares `g_RG_DDGIDebugOverlay` (RGBA16_FLOAT, screen res, UAV)
  - Composition: `DDGIDebugCompositor` (renderer in `DDGIRenderer.cpp`) runs after Bloom, reads `g_RG_TAAOutput` as UAV and `g_RG_DDGIDebugOverlay` as SRV, replaces pixels where overlay.a > 0
- [x] **Debug — CONVERGENCE_STATUS (mode 7):** ImGui-only overlay — per-volume tree with probe count (X×Y×Z → total), rays/frame (float → M), spacing per axis
- [x] Both modes (2, 7) in `DDGIDebugMode` combo box (ImGuiLayer.cpp)
- [x] **Verify:** Green/red probe dots visible inside the volume wireframe. ImGui shows probe trace stats. PIX/NSight confirms RayData UAV is written.

---

## Phase 4 — SDK blending pipeline + per-volume refactoring + RT probe visualization ✅

### SDK blending (8 passes dispatched via push-constant DDGIRootConstants)

- [x] Create nvrhi compute PSOs for all 8 SDK shaders (blending×2, classification×2, relocation×2, reduction×2)
- [x] Allocate `ProbeVariability` (R16_FLOAT, 2DArray) and `ProbeVariabilityAverage` (R16_FLOAT, 2DArray 1×1) textures
- [x] Textures moved from nvrhi direct creation → render-graph persistent allocation (via `DeclarePersistentTexture`)
- [x] `DDGIBlending.sr`: DDGIRootConstants as push constant (`[push_constant]` attribute on cbuffer, matches RTXGI D3D12 reference)
- [x] In `Render()`, after probe trace (Phase 3):
  1. `ProbeBlendingIrradianceCS` — `(probeCounts.x, .y, .z)` groups, 8×8 threads/group
  2. `ProbeBlendingDistanceCS` — `(probeCounts.x, .y, .z)` groups, 16×16 threads/group
  3. `ProbeRelocationCS` / `ProbeRelocationResetCS` (gated on `probeRelocationEnabled`)
  4. `ProbeClassificationCS` / `ProbeClassificationResetCS` (gated on `probeClassificationEnabled`)
  5. `DDGIReductionCS` → `DDGIExtraReductionCS` (gated on `probeVariabilityEnabled`)
- [x] All SDK passes use `bIncludeBindlessResources = true`; resources via `ResourceDescriptorHeap[index]`
- [x] `Renderer::RegisterTextureUAV()` + `RegisterStructuredBufferSRV()` — new bindless registration methods (inlined in `Renderer.h`)
- [x] Bindless registration deferred to first frame (`m_bBindlessRegistered` guard per volume)

### Per-volume refactoring

- [x] All texture handles (`RGTextureHandle`), bindless indices, texture dimensions, `m_ResourceIndicesBuffer` moved from renderer members into `DDGIVolumeNvrhi` (in `Scene.h`)
- [x] Renderer loops all volumes — zero `[0]` hardcoding in dispatch paths
- [x] `m_DDGIVolumesBuffer` remains as shared renderer state (all volumes packed into one struct buffer)

### Unit sphere mesh + TLAS for RT probe visualization

- [x] Unit sphere mesh (32×16 segments) in `CommonResources` — encapsulated in `CommonMesh` struct (`m_VertexBuffer`, `m_IndexBuffer`, `m_IndexCount`, `m_BLAS`)
- [x] Probe TLAS: `nvrhi::rt::InstanceDesc` per probe (sphere scale + grid position, `blasDeviceAddress` from unit-sphere BLAS)
- [x] Per-probe instance ID packing: `(volumeIndex << 16) | probeIndex`
- [x] `ProbeVisCS.hlsl` — compute shader with RayQuery, 8×8 thread groups, traces probe spheres, decodes instance ID
- [x] `ProbeVis.sr` / `ProbeVisInputs`: CB with `MatViewToWorld` + `MatWorldToClip` + `MatClipToWorld` (all `row_major`), TLAS, DDGIVolumes, Depth, overlay output
- [x] Depth test: project hit point to clip space via `MatrixMultiply(hitPos, MatWorldToClip)`, compare with depth buffer in reversed-Z, cull probes behind geometry

### Back-propagation (recursive indirect)

- [x] `ProbeTrace.sr` CB extended with `m_IrradianceTexIndex`, `m_DistanceTexIndex`, `m_ProbeDataTexIndex` — bindless heap indices
- [x] `ProbeTraceCS.hlsl`: after direct lighting, calls `DDGIGetVolumeIrradiance()` via bindless wrapper `SampleVolumeIrradiance()` — adds indirect to stored radiance for multi-bounce propagation
- [x] `Irradiance.hlsl` included via short path (`#include "ddgi/Irradiance.hlsl"`) — `CMakeLists.txt` dxc include path changed to `shaders/` level

### Compositor (separate from DeferredLighting)

- [x] `DDGIDebugCompositor` renderer (in `DDGIRenderer.cpp`, separate `REGISTER_RENDERER`)
- [x] Compute shader (`DDGIDebugCompositor.hlsl`): reads `g_RG_TAAOutput` as UAV (old HDR content), reads `g_RG_DDGIDebugOverlay` as SRV, replaces pixels where `overlay.a > 0`
- [x] Runs after Bloom (Read+Write `g_RG_TAAOutput` guarantees ordering before tonemap)
- [x] Removed DDGI overlay composition from `DeferredLighting.hlsl`

---

## Phase 4.5 — Probe debug fixes (camera, depth, composition, irradiance) 🔧

Phase 4 shipped the RT probe visualization but several issues prevent correct display:

### Camera matrix fixes

- [ ] `ProbeVisCS.hlsl` camera rays use `MatrixMultiply(clipPos, MatClipToWorld)` for world-pos reconstruction (matches PathTracer convention)
- [x] Camera position via `MatrixMultiply((0,0,0,1), MatViewToWorld).xyz` — correct with `row_major`
- [x] All matrix access uses `MatrixMultiply()` (project wrapper around `mul()`) — no raw `mul` or `transpose()`

### Probe centering

- [x] RTXGI SDK centres probes about origin: `pos = coords*spacing - shift + origin` where `shift = spacing*(counts-1)*0.5`
- [x] C++ instance placement and HLSL `worldPos` both apply the `-shift` offset

### Depth test

- [x] Reversed-Z: near=1 (closest), far≈0. Pixels with `sceneDepth < 1e-4` treated as sky/far-plane (always show probes)
- [x] Probe behind geometry when `probeNdcZ < sceneDepth` (reversed-Z: larger = closer)

### Irradiance/decode correctness

- [x] `ProbeVisCS` decode: `pow(raw, gamma*0.5)`, then `*self` (square), then `*(2*PI)` — matches SDK `DDGIGetVolumeIrradiance()` exactly. Was using `*2.0` instead of `*2*PI` (≈3× too dark)
- [x] U32 format branch: `color *= 0.5f` matches SDK `irradiance /= 2.0f` (U32 stores 2× for precision)
- [ ] Verify probe irradiance atlas is actually written by SDK blending passes (check in PIX: are blending passes dispatching? is RayData → irradiance blend working?)
- [ ] Verify back-propagation in ProbeTraceCS: `DDGIGetVolumeIrradiance()` samples last frame's atlas, adds to ray radiance
- [ ] Check `probeIrradianceEncodingGamma` value on the volume descriptor (default 5.0)

### SamplerDescriptorHeap pattern

- [x] Compute shaders cannot capture `SamplerState` locally in function body with SM 6.8
- [x] Workaround: `SampleDDGIAtlas()` file-scope helper wraps `Texture2DArray` + `SamplerState` capture (same pattern as `SampleBindlessTextureLevel` in `Bindless.hlsli`)

### Composition

- [x] `DDGIDebugCompositor` replaces DeferredLighting blend — runs after Bloom on `g_RG_TAAOutput`
- [x] Replace-blending (`overlay.a > 0 ? overlay.rgb : hdr`) — black probes (zero irradiance) still show as spheres

### To fix

- [ ] Probe irradiance samples might be stale (blending not writing to atlas) — verify SDK passes
- [ ] Probe overlay `color = float3(0.25,0.25,0.25)` fallback may be too dim after tonemap — consider brighter default

---

## Phase 5 — Indirect query CS + DDGI-only overlay debug

- [ ] Create `src/shaders/ddgi/IndirectQueryCS.hlsl`:
  - Fullscreen CS, one thread per 8×8 pixel tile
  - Reconstruct world position from depth
  - Loop all volumes: `DDGIGetVolumeBlendWeight()` → if >0, `DDGIGetVolumeIrradiance()` → accumulate
  - Output: `g_RG_DDGIIndirect` (RGBA16_FLOAT, screen resolution)
- [ ] Add to `shaders.cfg`
- [ ] In `Setup()`: declare `g_RG_DDGIIndirect` as transient RG texture, read depth/normals
- [ ] In `Render()`: dispatch indirect query CS after SDK blending
- [ ] **Debug — DDGI_INDIRECT_ONLY (mode 6):** Use `g_RG_DDGIIndirect` as-is for full-screen display (replace final color). Already available — no new shader, just branch in `Setup()` to route `g_RG_DebugOverlay` = `g_RG_DDGIIndirect`.
- [ ] Add mode 6 to the debug combo box
- [ ] **Verify:** Debug mode 6 shows colored indirect lighting (noisy, unconverged). `g_RG_DDGIIndirect` appears in render graph debug view.

---

## Phase 6 — Wire into DeferredRenderer

- [ ] In `DeferredRenderer::Setup()`: when `m_Mode == NormalBasic`, read `g_RG_DDGIIndirect`
- [ ] In `DeferredLighting.hlsl`: add `g_DDGIIndirect` texture binding
- [ ] In `DeferredLighting_PSMain`:
  ```hlsl
  if (g_Deferred.m_IndirectLightingMode == INDIRECT_LIGHTING_MODE_DDGI_SSGI)
      color += g_DDGIIndirect.Load(uint3(uvInt, 0)).rgb * albedo * (1.0 - metallic);
  ```
- [ ] **Verify:** Scene renders with DDGI indirect. Should see color bleeding. ImGui: toggle between None/ReSTIR GI/SHARC/ReSTIR+SHARC/DDGI+SSGI indirect lighting modes.

---

## Phase 7 — DDGI + SSGI confidence cascade blend + Compose debug modes

- [ ] In `SSGIRenderer::Setup()`: declare `g_RG_DDGIIndirect` as input read texture
- [ ] In `SSGICompose.hlsl`: after BRDF compose, blend with DDGI using confidence cascade (`DDGI_Analysis.md` §9.3.1):
  - Compute `ddgiConfidence` from aggregate volume weight, probe resolution ratio, probe distance
  - Compute `ssgiConfidence` from temporal age, screen-edge distance, hit success, disocclusion
  - Blend: `ddgiWeight = ddgiConfidence`, `ssgiWeight = (1-ddgiConfidence) * ssgiConfidence`, normalize
- [ ] When DDGI not enabled: `g_RG_DDGIIndirect` → black 1×1 → SSGI-only
- [ ] **Debug — DDGI_ONLY (mode 8):** Branch in `SSGICompose.hlsl` — output only DDGI contribution, zero SSGI. See `DDGI_Analysis.md` §4.13.3.
- [ ] **Debug — SSGI_ONLY (mode 9):** Branch in `SSGICompose.hlsl` — output only SSGI contribution, zero DDGI.
- [ ] **Debug — DDGI_CONFIDENCE_HEATMAP (mode 10):** `ddgiConfidence` → `lerp(red, green, confidence)`. See `DDGI_Analysis.md` §4.13.3.
- [ ] **Debug — VOLUME_BLEND_WEIGHT (mode 11):** `DDGIGetVolumeBlendWeight()` sum per pixel → cyan heatmap. See `DDGI_Analysis.md` §4.13.3.
- [ ] Add modes 8-11 to the debug combo box
- [ ] **Verify:** Toggle DDGI on/off while SSGI active — smooth transition. Confidence heatmap shows green in DDGI-covered areas, red at edges/gaps. SSGI_ONLY shows SSGI contribution, DDGI_ONLY shows DDGI. VOLUME_BLEND_WEIGHT shows volume influence zones.

---

## Phase 8 — Tile-based SSGI dispatch + Tile Activity debug

- [ ] Create `src/shaders/ddgi/TileClassifyCS.hlsl`:
  - 1 thread per 16×16 tile, 5-sample classification (4 corners + center)
  - Compute DDGI confidence per sample, mark tile active if any sample below threshold
  - Output: `g_TileMask` bitmask, `g_TileIndirectArgs`
- [ ] Add `g_TileMask` (RWStructuredBuffer) in `SSGIRenderer::Setup()`
- [ ] In each SSGI pass (RayMarch, Denoise): read tile mask at top, `return` if tile inactive
- [ ] Temporal reproject + Compose: always full-screen (`DDGI_Analysis.md` §9.8.4)
- [ ] Add `m_TileConfidenceThreshold` (default 0.80) to SSGI tuning params
- [ ] **Debug — TILE_ACTIVITY (mode 12):** Overlay tile mask on composed output — green=DDGI-sufficient (SSGI skipped), red=SSGI active. See `DDGI_Analysis.md` §4.13.3.
- [ ] Add mode 12 to the debug combo box
- [ ] **Verify:** Tile Activity debug shows green tiles where DDGI covers, red where SSGI runs. GPU profiler: SSGI pass times reduced in DDGI-covered areas.

---

## Phase 9 — Budgeted convergence system

- [ ] Add `DDGIUpdateBudget` struct to `DDGIRenderer` (`DDGI_Analysis.md` §4.6.1):
  - `m_MaxProbeRaysPerFrame = 500000`, `m_ConvergenceThreshold = 0.03f`, `m_ConvergenceMinFrames = 16`
- [ ] Per volume: track `m_Variability`, `m_FramesBelowThreshold`, `m_IsConverged`, `m_NextProbeOffset`
- [ ] Each frame: readback variability (async, prev frame value) → classify converged/unconverged
- [ ] Priority sort: `(1 - convergenceRatio) * probeCount * distanceToCamera`
- [ ] Allocate rays within budget; partial updates via `m_NextProbeOffset` round-robin
- [ ] On convergence: skip volume updates, redistribute budget
- [ ] Re-convergence triggers: dynamic object move, light change, manual re-bake
- [ ] **Debug — CONVERGENCE_STATUS (mode 7, expanded):** ImGui per-volume progress bars with name + % + variability value (see `DDGI_Analysis.md` §4.13.3 mode 10). Now driven by real convergence data from budget system.
- [ ] **Verify:** Convergence progress bars fill up over ~1-2s. Probe trace ray count drops as volumes converge.

---

## Phase 10 — Bake mode

- [ ] When all volumes converged: auto-set `m_EnableDDGI = false` (or manual toggle)
- [ ] Skip probe trace + SDK blending; keep indirect query from persistent textures
- [ ] Skip `TLASRenderer` when `m_EnableDDGI = false` (already conditional in NormalBasic branch)
- [ ] ImGui: show "Baked GI (no RT cost)" when converged, convergence progress otherwise
- [ ] **Verify:** RT cost drops to zero after convergence. Indirect lighting still visible from baked textures. Re-enabling DDGI resumes updates from current state.

---

## Phase 11 — Automatic volume placement

- [ ] Implement `DDGIVolumePlacer` class (`DDGI_Analysis.md` §4.12) called from `PostSceneLoad()`:
  1. **Voxelize**: scene AABB, 1m voxels, ray-cast classification (SURFACE/EMPTY_INDOOR/EMPTY_SKY/INSIDE_WALL)
  2. **Score**: probe efficiency per voxel
  3. **Components**: flood-fill connected SURFACE+EMPTY_INDOOR voxels
  4. **OBB fit**: PCA per component → `CandidateVolume { origin, eulerAngles, probeCounts }`
  5. **Greedy select**: sort by efficiency × size, coverage tracking, max 32 volumes
  6. **Post-process**: discard < 3×3×3, merge aligned neighbors, pad 1 probe row
- [ ] Replace hardcoded volume from Phase 2 with auto-placed volumes
- [ ] Volume Wireframe debug (mode 1) now shows all auto-placed OBBs
- [ ] **Verify:** Volume Wireframe shows OBBs for all auto-placed volumes. ImGui: print volume count, probe counts per volume, total probes. Bistro → ~10-18 volumes. Sponza → 1 volume.

---

## Phase 12 — Polish

- [ ] Add ImGui section: "DDGI" with enable checkbox, probe density presets, budget slider, convergence threshold
- [ ] Add DDGI-specific entries to `Config.cpp` CLI args: `--ddgi`, `--ddgiBudget=N`
- [ ] Test corner cases: thin geometry light leak, outdoor far distance, camera teleport, scene reload
- [ ] Profile: GPU timings for probe trace, SDK blend, indirect query. Compare DDGI vs SSGI cost.
- [ ] **Verify:** Full bake workflow: load scene → enable DDGI → wait convergence → bake → verify zero RT cost + correct indirect

---

## Dependency Order

```
Phase 1 ──► Phase 2 ──► Phase 2.5 ──► Phase 3 ──► Phase 4 ──► Phase 4.5 ──► Phase 5 ──► Phase 6
                                                                                         │
                   Phase 7 ◄────────────────────────────────────────────────────────────┘
                     │
                   Phase 8
                     │
                   Phase 9 ◄── Phase 11 (can be done in parallel after Phase 4)
                     │
                   Phase 10
                     │
                   Phase 11
                     │
                   Phase 12
```

> **Debug modes by phase:** Phase 2 → mode 1 (✅). Phase 3 → mode 2 (✅). Phase 4 → modes 2-5 infrastructure (✅). Phase 4.5 → modes 3,4,5 fixes (🔧). Phase 5 → mode 6. Phase 7 → modes 8,9,10,11. Phase 8 → mode 12. Phase 9 → mode 7 expanded.


## Shader Files to Create

| Phase | File | Profile | Purpose |
|---|---|---|---|
| 2 | `src/shaders/ddgi/VolumeWireframeCS.hlsl` | cs_6_8 | Debug mode 1 |
| 3 | `src/shaders/ddgi/ProbeTraceCS.hlsl` | cs_6_8 | Probe ray tracing ✅ |
| 3/4 | `src/shaders/ddgi/ProbeVisCS.hlsl` | cs_6_8 | Debug modes 2-5 ✅ (RT sphere viz, replaced ProbeOverlayCS) |
| 4 | `src/shaders/ddgi/ProbeVisUpdateCS.hlsl` | cs_6_8 | Probe TLAS instance transforms (not yet dispatched) |
| 4 | `src/shaders/DDGIDebugCompositor.hlsl` | cs_6_8 | Additive-blend overlay onto TAAOutput ✅ |
| 5 | `src/shaders/ddgi/IndirectQueryCS.hlsl` | cs_6_8 | Fullscreen irradiance gather |
| 8 | `src/shaders/ddgi/TileClassifyCS.hlsl` | cs_6_8 | Tile mask generation |

**Deleted:** `ProbeOverlayCS.hlsl`, `ProbeOverlay.sr`, `ProbeTextureDebugCS.hlsl` — superseded by `ProbeVisCS.hlsl`.

## Key Render Graph Handles

| Handle | Phase | Persistent? | Format | Notes |
|---|---|---|---|---|
| DDGI volume textures | 2 | **persistent** | various | Survive bake mode |
| `g_RG_DDGIDebugOverlay` | 3 | transient | RGBA16_FLOAT | Screen res — debug viz overlay (mode 2+) |
| `g_RG_DDGIIndirect` | 5 | transient | RGBA16_FLOAT | Screen res — indirect gather |
| `g_TileMask` | 8 | transient | R32_UINT | Tile bitmask |
