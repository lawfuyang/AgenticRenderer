# DDGI Implementation Plan

> For AI agent. Granular phases. Each phase = visual verification possible via debug modes.  
> Debug modes are spread across phases — each phase adds the debug visualization for data it produces.  
> Source: `docs/DDGI_Analysis.md` §4.13 (Debug Modes) + §8 (Roadmap).

---

## Debug Mode Enum (built incrementally)

```cpp
enum class DDGIDebugMode : uint32_t {
    OFF                      = 0,  // Normal rendering
    VOLUME_WIREFRAME         = 1,  // OBB wireframes (Phase 2)
    PROBE_POSITIONS          = 2,  // Probe sphere overlay (Phase 3)
    PROBE_IRRADIANCE         = 3,  // Raw irradiance sample (Phase 4)
    PROBE_DISTANCE           = 4,  // Raw distance sample (Phase 4)
    PROBE_CLASSIFICATION     = 5,  // State heatmap overlay (Phase 4)
    DDGI_INDIRECT_ONLY       = 6,  // Raw g_RG_DDGIIndirect output (Phase 5)
    CONVERGENCE_STATUS       = 7,  // ImGui progress bars (Phase 3+, expanded Phase 9)
    DDGI_ONLY                = 8,  // Final composed DDGI only (Phase 7)
    SSGI_ONLY                = 9,  // Final composed SSGI only (Phase 7)
    DDGI_CONFIDENCE_HEATMAP  = 10, // Red=low, green=high (Phase 7)
    VOLUME_BLEND_WEIGHT      = 11, // Cyan heatmap (Phase 7)
    TILE_ACTIVITY            = 12, // SSGI dispatch mask (Phase 8)
};
```

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

## Phase 2 — 1 hardcoded volume + Volume Wireframe debug

- [ ] In `PostSceneLoad()`: create 1 `rtxgi::DDGIVolumeDesc` with hardcoded AABB around scene origin (e.g. 20×10×20, 1.5m spacing)
- [ ] Call `rtxgi::d3d12::DDGIVolume::Create(desc, resources)`
- [ ] Allocate persistent textures: irradiance (R10G10B10A2), distance (RG16), probe data (RGBA16)
- [ ] Declare textures in `Setup()` as persistent RG resources
- [ ] **Debug — VOLUME_WIREFRAME (mode 1):** Create `src/shaders/ddgi/VolumeWireframeCS.hlsl` — one thread group per volume, projects OBB edges into screen space, draws 1px lines on `g_RG_DebugOverlay` (RGBA16_FLOAT, screen res). See `DDGI_Analysis.md` §4.13.3.
- [ ] Add `g_RG_DebugOverlay` transient RG handle in `Setup()`
- [ ] Add `m_DDGIDebugMode` to `DDGIRenderer`; wire ImGui combo box in DDGI tab
- [ ] **Verify:** Green wireframe box visible in scene at the hardcoded AABB position. No crash, no GPU validation errors. ImGui: print volume desc (probe counts, spacing, memory usage).

---

## Phase 3 — Probe trace CS + Probe Position debug + Convergence Status

- [ ] Flesh out `src/shaders/ddgi/ProbeTraceCS.hlsl`:
  - Load `DDGIVolumeDescGPU` constants (bindless, structured buffer via descriptor heap)
  - Compute probe index + ray index from `DispatchThreadID`
  - `DDGIGetProbeCoords()` → probe grid coords
  - `DDGIGetProbeWorldPosition()` → world position
  - `DDGIGetProbeRayDirection(rayIndex, volume)` → random-ish ray direction
  - `RayQuery` inline RT against TLAS
  - On hit: compute direct lighting (sun only) at hit point, store radiance + hitT in RayData UAV
  - On miss: store sky radiance + large hitT
- [ ] Create compute PSO for `ProbeTraceCS` using `nvrhi`
- [ ] In `Render()`: `DDGIVolume::Update()` → upload constants → `Dispatch(probeRayCount / 64, 1, 1)`
- [ ] By end of phase, `src/shaders/ddgi/ProbeTraceCS.hlsl` is the current stub — this phase makes it real.
- [ ] **Debug — PROBE_POSITIONS (mode 2):** Create `src/shaders/ddgi/ProbeOverlayCS.hlsl` — 1 thread per probe, project world pos to screen, depth-test against `g_Depth`, draw 3×3 dot. Color: active=green, inactive=red (from `g_ProbeData.w`). See `DDGI_Analysis.md` §4.13.1.
- [ ] **Debug — CONVERGENCE_STATUS (mode 7):** ImGui-only: print per-volume probe count, rays dispatched/frame, GPU timing from microprofile.
- [ ] Add both modes to the `DDGIDebugMode` combo box
- [ ] **Verify:** Green/red probe dots visible inside the volume wireframe. ImGui shows probe trace stats. PIX/NSight confirms RayData UAV is written.

---

## Phase 4 — SDK blending pipeline + Irradiance/Distance/Classification debug

- [ ] Load compiled SDK DXIL blobs (from `bin/shaders/dxil/`) into `ShaderBlob`:
  - `ProbeBlendingCS` (irradiance + distance variants)
  - `ProbeClassificationCS` (update + reset)
  - `ProbeRelocationCS` (update + reset)
  - `ReductionCS` (main + extra)
- [ ] Create `DDGIVolumeManagedResourcesDesc` with all bytecode
- [ ] In `Render()`, after probe trace:
  1. Transition RayData to SRV
  2. `UpdateDDGIVolumeProbes()` → blends new ray data into irradiance/distance textures
  3. `RelocateDDGIVolumeProbes()` (optional, behind flag)
  4. `ClassifyDDGIVolumeProbes()` (optional, behind flag)
- [ ] **Debug — PROBE_IRRADIANCE (mode 3):** Sample DDGI irradiance texture directly per pixel in the indirect query / debug overlay shader. Decode from gamma, scale by 2*PI for display. See `DDGI_Analysis.md` §4.13.3.
- [ ] **Debug — PROBE_DISTANCE (mode 4):** Sample DDGI distance texture directly per pixel.
- [ ] **Debug — PROBE_CLASSIFICATION (mode 5):** Extend `ProbeOverlayCS` to color probes by classification state (active/inactive/relocated from `g_ProbeData` fields).
- [ ] Add modes 3-5 to the debug combo box
- [ ] **Verify:** Irradiance/distance debug modes show colored content (not black). Classification overlay colors probes by state. ImGui: toggle relocation/classification on/off, observe probe state changes.

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
Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 4 ──► Phase 5 ──► Phase 6
                                                                    │
                Phase 7 ◄───────────────────────────────────────────┘
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

> **Debug modes by phase:** Phase 2 → mode 1. Phase 3 → modes 2, 7. Phase 4 → modes 3, 4, 5. Phase 5 → mode 6. Phase 7 → modes 8, 9, 10, 11. Phase 8 → mode 12. Phase 9 → mode 7 expanded.

## Shader Files to Create

| Phase | File | Profile | Purpose |
|---|---|---|---|
| 2 | `src/shaders/ddgi/VolumeWireframeCS.hlsl` | cs_6_8 | Debug mode 1 |
| 3 | `src/shaders/ddgi/ProbeTraceCS.hlsl` | cs_6_8 | Probe ray tracing |
| 3 | `src/shaders/ddgi/ProbeOverlayCS.hlsl` | cs_6_8 | Debug modes 2, 5 |
| 5 | `src/shaders/ddgi/IndirectQueryCS.hlsl` | cs_6_8 | Fullscreen irradiance gather |
| 8 | `src/shaders/ddgi/TileClassifyCS.hlsl` | cs_6_8 | Tile mask generation |

## Key Render Graph Handles

| Handle | Phase | Persistent? | Format | Notes |
|---|---|---|---|---|
| DDGI volume textures | 2 | **persistent** | various | Survive bake mode |
| `g_RG_DebugOverlay` | 2 | transient | RGBA16_FLOAT | Screen res — debug viz output |
| `g_RG_DDGIIndirect` | 5 | transient | RGBA16_FLOAT | Screen res — indirect gather |
| `g_TileMask` | 8 | transient | R32_UINT | Tile bitmask |
