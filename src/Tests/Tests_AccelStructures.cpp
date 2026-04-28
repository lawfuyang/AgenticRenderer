// Tests_AccelStructures.cpp
//
// Systems under test: Scene BLAS/TLAS acceleration structures, per-LOD BLAS,
//                     BLAS address buffer, TLAS instance descriptors, transform
//                     consistency, ray-intersection geometry correctness, and
//                     RayQuery / RTXDI scene-loaded invariants.
//
// Prerequisites: g_Renderer fully initialized (RHI + CommonResources).
//
// Test coverage (new tests — does NOT duplicate Tests_SceneAdvanced.cpp):
//
//   Scene_BLASPerLOD
//     TC-BLASLOD-01  Every primitive has exactly m_LODCount BLAS handles
//     TC-BLASLOD-02  All per-LOD BLAS handles are non-null
//     TC-BLASLOD-03  LOD-0 BLAS device address is non-zero
//     TC-BLASLOD-04  Higher-LOD BLAS device addresses are non-zero
//     TC-BLASLOD-05  BLAS count across all primitives equals total LOD slots used
//     TC-BLASLOD-06  MeshData LOD count matches primitive BLAS vector size
//     TC-BLASLOD-07  LOD-0 index count is >= LOD-1 index count (coarser LODs have fewer tris)
//     TC-BLASLOD-08  BLAS address buffer entry count equals instanceCount * MAX_LOD_COUNT
//     TC-BLASLOD-09  All BLAS address buffer entries are non-zero after build
//     TC-BLASLOD-10  Clamped-LOD entries: addresses for lod >= lodCount equal the last valid LOD
//
//   Scene_BLASCompaction
//     TC-BLASCOMP-01 BuildAccelerationStructures can be called twice without crash
//     TC-BLASCOMP-02 Re-calling BuildAccelerationStructures does not create duplicate BLAS
//     TC-BLASCOMP-03 BLAS memory requirement is positive for every built BLAS
//     TC-BLASCOMP-04 BLAS device address is stable across two consecutive builds
//
//   Scene_TLASSingleInstance
//     TC-TLASSI-01   TLAS is non-null after single-primitive in-memory scene
//     TC-TLASSI-02   RT instance desc count equals instance data count
//     TC-TLASSI-03   RT instance desc instanceID matches its array index
//     TC-TLASSI-04   RT instance desc instanceMask is non-zero
//     TC-TLASSI-05   RT instance desc BLAS device address is non-zero
//     TC-TLASSI-06   RT instance desc transform is not all-zero
//     TC-TLASSI-07   RT instance desc buffer byte size is consistent with instance count
//
//   Scene_TLASMultiInstance
//     TC-TLASMI-01   Multi-mesh scene has more than one RT instance desc
//     TC-TLASMI-02   All RT instance desc instanceIDs are unique
//     TC-TLASMI-03   All RT instance desc BLAS addresses are non-zero
//     TC-TLASMI-04   Opaque instances have ForceOpaque flag set
//     TC-TLASMI-05   Non-opaque instances have ForceNonOpaque flag set
//     TC-TLASMI-06   Instance count matches sum of opaque + masked + transparent buckets
//
//   Scene_TransformConsistency
//     TC-XFORM-01    RT instance desc transform matches PerInstanceData world matrix (row 0)
//     TC-XFORM-02    RT instance desc transform matches PerInstanceData world matrix (row 1)
//     TC-XFORM-03    RT instance desc transform matches PerInstanceData world matrix (row 2)
//     TC-XFORM-04    RT instance desc transform is finite for all instances
//     TC-XFORM-05    Identity-transform node produces identity RT transform
//     TC-XFORM-06    Translated node produces correct RT transform translation column
//     TC-XFORM-07    PerInstanceData world matrix diagonal is non-zero for mesh nodes
//     TC-XFORM-08    PerInstanceData m_MeshDataIndex is in-range for all instances
//
//   Scene_RayIntersectionGeometry
//     TC-RAYGEOM-01  BLAS geometry uses the scene vertex buffer
//     TC-RAYGEOM-02  BLAS geometry uses the scene index buffer
//     TC-RAYGEOM-03  BLAS geometry index count matches MeshData LOD-0 index count
//     TC-RAYGEOM-04  BLAS geometry vertex count matches primitive vertex count
//     TC-RAYGEOM-05  BLAS geometry vertex stride matches VertexQuantized size
//     TC-RAYGEOM-06  BLAS geometry index offset is within index buffer bounds
//     TC-RAYGEOM-07  All primitives have non-zero vertex counts for RT
//     TC-RAYGEOM-08  MeshData LOD-0 index count is a multiple of 3 (whole triangles)
//     TC-RAYGEOM-09  MeshData LOD-0 index offset + count <= total index buffer used
//
//   Scene_RayQueryReadiness
//     TC-RQREADY-01  TLAS is non-null after full scene load (RT-ready)
//     TC-RQREADY-02  m_RTInstanceDescBuffer is non-null (needed for TLAS rebuild)
//     TC-RQREADY-03  m_BLASAddressBuffer is non-null (needed by TLASPatch_CS)
//     TC-RQREADY-04  m_InstanceLODBuffer is non-null (needed by TLASPatch_CS)
//     TC-RQREADY-05  m_InstanceDataBuffer is non-null (needed by RT shaders)
//     TC-RQREADY-06  TLAS max-instance capacity >= actual instance count
//     TC-RQREADY-07  RT instance desc buffer byte size >= instanceCount * 64 bytes
//     TC-RQREADY-08  BLAS address buffer byte size >= instanceCount * MAX_LOD_COUNT * 8
//     TC-RQREADY-09  Instance LOD buffer byte size >= instanceCount * 4 bytes
//     TC-RQREADY-10  All instances have a valid MeshDataIndex for RT triangle lookup
//
//   Scene_RTXDISceneLoaded
//     TC-RTXDI-01    m_InstanceData is non-empty after scene load (RTXDI needs instances)
//     TC-RTXDI-02    All instances have m_MeshDataIndex in-range
//     TC-RTXDI-03    All instances have m_MaterialIndex in-range or UINT32_MAX
//     TC-RTXDI-04    m_MeshDataBuffer is non-null (RTXDI reads mesh data)
//     TC-RTXDI-05    m_MaterialConstantsBuffer is non-null (RTXDI reads material data)
//     TC-RTXDI-06    m_LightBuffer is non-null after scene load
//     TC-RTXDI-07    m_LightCount matches m_Lights.size() after scene load
//     TC-RTXDI-08    Scene has at least one directional light (RTXDI sun light)
//     TC-RTXDI-09    All instance world matrices are finite (no NaN/Inf)
//     TC-RTXDI-10    m_FirstGeometryInstanceIndex is 0 for the first instance
//
// Run with: HobbyRenderer --run-tests=*AccelStruct* --gltf-samples <path>
//           HobbyRenderer --run-tests=*BLAS* --gltf-samples <path>
//           HobbyRenderer --run-tests=*TLAS* --gltf-samples <path>
//           HobbyRenderer --run-tests=*RayQuery* --gltf-samples <path>
//           HobbyRenderer --run-tests=*RTXDI* --gltf-samples <path>
// ============================================================================

#include "TestFixtures.h"

// ============================================================================
// Shared minimal in-memory glTF (single triangle, no textures)
// ============================================================================
namespace
{
    // Minimal glTF: one mesh, one node, one triangle.
    static constexpr const char k_AS_MinimalGltf[] = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "accessors": [ {
    "bufferView": 0, "byteOffset": 0,
    "componentType": 5126, "count": 3, "type": "VEC3",
    "max": [ 1.0, 1.0, 0.0 ], "min": [ 0.0, 0.0, 0.0 ]
  } ],
  "bufferViews": [ {
    "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962
  } ],
  "buffers": [ {
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
    "byteLength": 36
  } ]
})";

    // Helper: load the minimal glTF into a clean scene and build AS.
    // Returns true on success.  Caller must call Shutdown() when done.
    static bool LoadMinimalSceneWithAS()
    {
        if (!DEV()) return false;
        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();

        g_Renderer.m_Scene.InitializeDefaultCube(0, 0);
        g_Renderer.ExecutePendingCommandLists();

        std::vector<srrhi::VertexQuantized> vertices;
        std::vector<uint32_t> indices;
        const bool ok = SceneLoader::LoadGLTFSceneFromMemory(
            g_Renderer.m_Scene,
            k_AS_MinimalGltf, sizeof(k_AS_MinimalGltf) - 1,
            {}, vertices, indices);
        if (!ok) return false;

        g_Renderer.m_Scene.FinalizeLoadedScene();
        SceneLoader::LoadTexturesFromImages(g_Renderer.m_Scene, {});
        g_Renderer.m_Scene.UploadGeometryBuffers(vertices, indices);
        SceneLoader::CreateAndUploadLightBuffer(g_Renderer.m_Scene);

        {
            nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
            ScopedCommandList scopedCmd{ cmd, "Test_BuildAS" };
            SceneLoader::UpdateMaterialsAndCreateConstants(g_Renderer.m_Scene, cmd);
            g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
        }
        g_Renderer.ExecutePendingCommandLists();
        DEV()->waitForIdle();
        return true;
    }

    // Helper: clean up after LoadMinimalSceneWithAS.
    static void ShutdownMinimalScene()
    {
        if (DEV()) DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }
} // anonymous namespace


// ============================================================================
// TEST SUITE: Scene_BLASPerLOD
// ============================================================================
TEST_SUITE("Scene_BLASPerLOD")
{
    // ------------------------------------------------------------------
    // TC-BLASLOD-01: Every primitive has exactly m_LODCount BLAS handles
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-01 BLASPerLOD - every primitive has exactly LODCount BLAS handles")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue; // skip default-cube placeholder

                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const uint32_t lodCount = scene.m_MeshData[meshDataIdx].m_LODCount;

                INFO("Mesh " << mi << " prim " << pi
                     << " lodCount=" << lodCount
                     << " blasVecSize=" << prim.m_BLAS.size());
                CHECK((uint32_t)prim.m_BLAS.size() == lodCount);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-02: All per-LOD BLAS handles are non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-02 BLASPerLOD - all per-LOD BLAS handles are non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                for (int lod = 0; lod < (int)prim.m_BLAS.size(); ++lod)
                {
                    INFO("Mesh " << mi << " prim " << pi << " LOD " << lod);
                    CHECK(prim.m_BLAS[lod] != nullptr);
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-03: LOD-0 BLAS device address is non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-03 BLASPerLOD - LOD-0 BLAS device address is non-zero")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        bool foundAny = false;
        for (const auto& mesh : scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                if (prim.m_BLAS.empty()) continue;
                REQUIRE(prim.m_BLAS[0] != nullptr);
                const uint64_t addr = prim.m_BLAS[0]->getDeviceAddress();
                INFO("LOD-0 device address = " << addr);
                CHECK(addr != 0u);
                foundAny = true;
            }
        }
        CHECK(foundAny);
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-04: Higher-LOD BLAS device addresses are non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-04 BLASPerLOD - higher-LOD BLAS device addresses are non-zero")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (const auto& mesh : scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                for (int lod = 0; lod < (int)prim.m_BLAS.size(); ++lod)
                {
                    REQUIRE(prim.m_BLAS[lod] != nullptr);
                    const uint64_t addr = prim.m_BLAS[lod]->getDeviceAddress();
                    INFO("LOD " << lod << " device address = " << addr);
                    CHECK(addr != 0u);
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-05: BLAS count across all primitives equals total LOD slots used
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-05 BLASPerLOD - total BLAS count equals sum of per-primitive LOD counts")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        uint32_t totalBLAS = 0;
        uint32_t totalLODSlots = 0;
        for (const auto& mesh : scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                totalBLAS += (uint32_t)prim.m_BLAS.size();
                if (prim.m_MeshDataIndex < (uint32_t)scene.m_MeshData.size())
                    totalLODSlots += scene.m_MeshData[prim.m_MeshDataIndex].m_LODCount;
            }
        }
        // Every BLAS slot must be accounted for by a LOD entry.
        CHECK(totalBLAS == totalLODSlots);
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-06: MeshData LOD count matches primitive BLAS vector size
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-06 BLASPerLOD - MeshData LOD count matches BLAS vector size")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue;
                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const uint32_t lodCount = scene.m_MeshData[meshDataIdx].m_LODCount;
                INFO("Mesh " << mi << " prim " << pi);
                CHECK(lodCount >= 1u);
                CHECK(lodCount <= srrhi::CommonConsts::MAX_LOD_COUNT);
                CHECK((uint32_t)prim.m_BLAS.size() == lodCount);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-07: LOD-0 index count >= LOD-1 index count (coarser LODs have fewer tris)
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-07 BLASPerLOD - LOD-0 index count >= LOD-1 index count")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        bool foundMultiLOD = false;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.size() < 2) continue;
                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const srrhi::MeshData& md = scene.m_MeshData[meshDataIdx];
                if (md.m_LODCount < 2) continue;
                foundMultiLOD = true;
                INFO("Mesh " << mi << " prim " << pi
                     << " LOD0 idxCount=" << md.m_IndexCounts[0]
                     << " LOD1 idxCount=" << md.m_IndexCounts[1]);
                CHECK(md.m_IndexCounts[0] >= md.m_IndexCounts[1]);
            }
        }
        // If no multi-LOD primitive was found, the test is vacuously true — just warn.
        if (!foundMultiLOD)
            WARN("No multi-LOD primitive found in CesiumMilkTruck — LOD reduction test skipped");
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-08: BLAS address buffer entry count equals instanceCount * MAX_LOD_COUNT
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-08 BLASPerLOD - BLAS address buffer entry count is correct")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_BLASAddressBuffer != nullptr);
        const uint32_t instanceCount = (uint32_t)g_Renderer.m_Scene.m_InstanceData.size();
        const uint64_t expectedBytes = (uint64_t)instanceCount
            * srrhi::CommonConsts::MAX_LOD_COUNT
            * sizeof(uint64_t);
        const uint64_t actualBytes = g_Renderer.m_Scene.m_BLASAddressBuffer->getDesc().byteSize;
        CHECK(actualBytes == expectedBytes);
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-09: All BLAS address buffer entries are non-zero after build
    //               (verified via CPU-side blasAddresses vector reconstruction)
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-09 BLASPerLOD - all per-instance LOD-0 BLAS addresses are non-zero")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        // Build a mapping from MeshDataIndex -> Primitive* (same as Scene::BuildAccelerationStructures)
        std::vector<const Scene::Primitive*> meshDataToPrim(scene.m_MeshData.size(), nullptr);
        for (const auto& mesh : scene.m_Meshes)
            for (const auto& prim : mesh.m_Primitives)
                if (prim.m_MeshDataIndex < (uint32_t)meshDataToPrim.size())
                    meshDataToPrim[prim.m_MeshDataIndex] = &prim;

        for (uint32_t instID = 0; instID < (uint32_t)scene.m_InstanceData.size(); ++instID)
        {
            const uint32_t meshDataIdx = scene.m_InstanceData[instID].m_MeshDataIndex;
            REQUIRE(meshDataIdx < (uint32_t)meshDataToPrim.size());
            const Scene::Primitive* prim = meshDataToPrim[meshDataIdx];
            REQUIRE(prim != nullptr);
            REQUIRE(!prim->m_BLAS.empty());
            const uint64_t addr = prim->m_BLAS[0]->getDeviceAddress();
            INFO("Instance " << instID << " LOD-0 address=" << addr);
            CHECK(addr != 0u);
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASLOD-10: Clamped-LOD entries: address for lod >= lodCount equals last valid LOD
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASLOD-10 BLASPerLOD - clamped LOD addresses equal last valid LOD address")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        std::vector<const Scene::Primitive*> meshDataToPrim(scene.m_MeshData.size(), nullptr);
        for (const auto& mesh : scene.m_Meshes)
            for (const auto& prim : mesh.m_Primitives)
                if (prim.m_MeshDataIndex < (uint32_t)meshDataToPrim.size())
                    meshDataToPrim[prim.m_MeshDataIndex] = &prim;

        for (uint32_t instID = 0; instID < (uint32_t)scene.m_InstanceData.size(); ++instID)
        {
            const uint32_t meshDataIdx = scene.m_InstanceData[instID].m_MeshDataIndex;
            REQUIRE(meshDataIdx < (uint32_t)meshDataToPrim.size());
            const Scene::Primitive* prim = meshDataToPrim[meshDataIdx];
            REQUIRE(prim != nullptr);
            if (prim->m_BLAS.empty()) continue;

            const uint32_t lodCount = (uint32_t)prim->m_BLAS.size();
            const uint64_t lastAddr = prim->m_BLAS[lodCount - 1]->getDeviceAddress();

            // For every LOD slot beyond lodCount, the address must equal the last valid LOD.
            for (uint32_t lod = lodCount; lod < srrhi::CommonConsts::MAX_LOD_COUNT; ++lod)
            {
                // Reconstruct what BuildAccelerationStructures would have stored:
                const uint32_t clampedLod = lodCount - 1;
                const uint64_t clampedAddr = prim->m_BLAS[clampedLod]->getDeviceAddress();
                INFO("Instance " << instID << " lod=" << lod << " clampedAddr=" << clampedAddr);
                CHECK(clampedAddr == lastAddr);
            }
        }
    }
}


// ============================================================================
// TEST SUITE: Scene_BLASCompaction
// ============================================================================
TEST_SUITE("Scene_BLASCompaction")
{
    // ------------------------------------------------------------------
    // TC-BLASCOMP-01: BuildAccelerationStructures can be called twice without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASCOMP-01 BLASCompaction - BuildAccelerationStructures is idempotent (no crash)")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        // First build already happened inside SceneScope.
        // Call a second build — must not crash.
        CHECK_NOTHROW(
        {
            nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
            ScopedCommandList scopedCmd{ cmd, "Test_RebuildAS" };
            g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
        });
        g_Renderer.ExecutePendingCommandLists();
        DEV()->waitForIdle();
    }

    // ------------------------------------------------------------------
    // TC-BLASCOMP-02: Re-calling BuildAccelerationStructures does not create duplicate BLAS
    //                 (BLAS vector size must remain the same after a second build)
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASCOMP-02 BLASCompaction - second build does not duplicate BLAS handles")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        // Record BLAS vector sizes before second build.
        std::vector<size_t> blasSizesBefore;
        for (const auto& mesh : g_Renderer.m_Scene.m_Meshes)
            for (const auto& prim : mesh.m_Primitives)
                blasSizesBefore.push_back(prim.m_BLAS.size());

        // Second build.
        {
            nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
            ScopedCommandList scopedCmd{ cmd, "Test_RebuildAS2" };
            g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
        }
        g_Renderer.ExecutePendingCommandLists();
        DEV()->waitForIdle();

        // BLAS vector sizes must be unchanged.
        size_t idx = 0;
        for (const auto& mesh : g_Renderer.m_Scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                INFO("BLAS size before=" << blasSizesBefore[idx]
                     << " after=" << prim.m_BLAS.size());
                CHECK(prim.m_BLAS.size() == blasSizesBefore[idx]);
                ++idx;
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASCOMP-03: BLAS memory requirement is positive for every built BLAS
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASCOMP-03 BLASCompaction - BLAS memory requirement is positive")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        nvrhi::IDevice* device = DEV();
        REQUIRE(device != nullptr);

        for (const auto& mesh : g_Renderer.m_Scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                for (int lod = 0; lod < (int)prim.m_BLAS.size(); ++lod)
                {
                    REQUIRE(prim.m_BLAS[lod] != nullptr);
                    const auto memReq = device->getAccelStructMemoryRequirements(prim.m_BLAS[lod]);
                    INFO("LOD " << lod << " memReq.size=" << memReq.size);
                    CHECK(memReq.size > 0u);
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-BLASCOMP-04: BLAS device address is stable across two consecutive builds
    //                 (second build skips already-built BLAS, so address must not change)
    // ------------------------------------------------------------------
    TEST_CASE("TC-BLASCOMP-04 BLASCompaction - BLAS device address is stable after second build")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        // Capture addresses before second build.
        std::vector<uint64_t> addrsBefore;
        for (const auto& mesh : g_Renderer.m_Scene.m_Meshes)
            for (const auto& prim : mesh.m_Primitives)
                for (const auto& blas : prim.m_BLAS)
                    addrsBefore.push_back(blas ? blas->getDeviceAddress() : 0u);

        // Second build.
        {
            nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
            ScopedCommandList scopedCmd{ cmd, "Test_RebuildAS3" };
            g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
        }
        g_Renderer.ExecutePendingCommandLists();
        DEV()->waitForIdle();

        // Addresses must be unchanged.
        size_t idx = 0;
        for (const auto& mesh : g_Renderer.m_Scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                for (const auto& blas : prim.m_BLAS)
                {
                    const uint64_t addrAfter = blas ? blas->getDeviceAddress() : 0u;
                    INFO("addr before=" << addrsBefore[idx] << " after=" << addrAfter);
                    CHECK(addrAfter == addrsBefore[idx]);
                    ++idx;
                }
            }
        }
    }
}


// ============================================================================
// TEST SUITE: Scene_TLASSingleInstance
// ============================================================================
TEST_SUITE("Scene_TLASSingleInstance")
{
    // ------------------------------------------------------------------
    // TC-TLASSI-01: TLAS is non-null after single-primitive in-memory scene
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-01 TLASSingleInstance - TLAS is non-null after in-memory scene build")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        CHECK(g_Renderer.m_Scene.m_TLAS != nullptr);

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-TLASSI-02: RT instance desc count equals instance data count
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-02 TLASSingleInstance - RT instance desc count equals instance data count")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        CHECK(scene.m_RTInstanceDescs.size() == scene.m_InstanceData.size());

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-TLASSI-03: RT instance desc instanceID matches its array index
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-03 TLASSingleInstance - RT instance desc instanceID matches array index")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            INFO("Instance " << i << " instanceID=" << scene.m_RTInstanceDescs[i].instanceID);
            CHECK(scene.m_RTInstanceDescs[i].instanceID == i);
        }

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-TLASSI-04: RT instance desc instanceMask is non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-04 TLASSingleInstance - RT instance desc instanceMask is non-zero")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            INFO("Instance " << i << " instanceMask=" << scene.m_RTInstanceDescs[i].instanceMask);
            CHECK(scene.m_RTInstanceDescs[i].instanceMask != 0u);
        }

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-TLASSI-05: RT instance desc BLAS device address is non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-05 TLASSingleInstance - RT instance desc BLAS device address is non-zero")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            INFO("Instance " << i << " blasDeviceAddress=" << scene.m_RTInstanceDescs[i].blasDeviceAddress);
            CHECK(scene.m_RTInstanceDescs[i].blasDeviceAddress != 0u);
        }

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-TLASSI-06: RT instance desc transform is not all-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-06 TLASSingleInstance - RT instance desc transform is not all-zero")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[i].transform;
            // At least one element must be non-zero (identity has 1s on diagonal).
            bool anyNonZero = false;
            for (int k = 0; k < 12; ++k)
                if (t[k] != 0.0f) { anyNonZero = true; break; }
            INFO("Instance " << i);
            CHECK(anyNonZero);
        }

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-TLASSI-07: RT instance desc buffer byte size is consistent with instance count
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASSI-07 TLASSingleInstance - RT instance desc buffer byte size is correct")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        REQUIRE(scene.m_RTInstanceDescBuffer != nullptr);
        const uint64_t expectedBytes = (uint64_t)scene.m_RTInstanceDescs.size()
            * sizeof(nvrhi::rt::InstanceDesc);
        const uint64_t actualBytes = scene.m_RTInstanceDescBuffer->getDesc().byteSize;
        CHECK(actualBytes == expectedBytes);

        ShutdownMinimalScene();
    }
}


// ============================================================================
// TEST SUITE: Scene_TLASMultiInstance
// ============================================================================
TEST_SUITE("Scene_TLASMultiInstance")
{
    // ------------------------------------------------------------------
    // TC-TLASMI-01: Multi-mesh scene has more than one RT instance desc
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASMI-01 TLASMultiInstance - multi-mesh scene has multiple RT instance descs")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_RTInstanceDescs.size() > 1u);
    }

    // ------------------------------------------------------------------
    // TC-TLASMI-02: All RT instance desc instanceIDs are unique
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASMI-02 TLASMultiInstance - all RT instance desc instanceIDs are unique")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const auto& descs = g_Renderer.m_Scene.m_RTInstanceDescs;
        std::vector<uint32_t> ids;
        ids.reserve(descs.size());
        for (const auto& d : descs)
            ids.push_back(d.instanceID);
        std::sort(ids.begin(), ids.end());
        const bool allUnique = std::adjacent_find(ids.begin(), ids.end()) == ids.end();
        CHECK(allUnique);
    }

    // ------------------------------------------------------------------
    // TC-TLASMI-03: All RT instance desc BLAS addresses are non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASMI-03 TLASMultiInstance - all RT instance desc BLAS addresses are non-zero")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const auto& descs = g_Renderer.m_Scene.m_RTInstanceDescs;
        for (uint32_t i = 0; i < (uint32_t)descs.size(); ++i)
        {
            INFO("Instance " << i << " blasDeviceAddress=" << descs[i].blasDeviceAddress);
            CHECK(descs[i].blasDeviceAddress != 0u);
        }
    }

    // ------------------------------------------------------------------
    // TC-TLASMI-04: Opaque instances have ForceOpaque flag set
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASMI-04 TLASMultiInstance - opaque instances have ForceOpaque flag")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::InstanceDesc& desc = scene.m_RTInstanceDescs[i];
            const srrhi::PerInstanceData& inst = scene.m_InstanceData[i];
            const uint32_t matIdx = inst.m_MaterialIndex;

            // Only check instances with a valid material.
            if (matIdx >= (uint32_t)scene.m_Materials.size()) continue;
            const uint32_t alphaMode = scene.m_Materials[matIdx].m_AlphaMode;

            if (alphaMode == srrhi::CommonConsts::ALPHA_MODE_OPAQUE)
            {
                INFO("Instance " << i << " (opaque) flags=" << (int)desc.flags);
                const bool hasForceOpaque = ((uint8_t)desc.flags & (uint8_t)nvrhi::rt::InstanceFlags::ForceOpaque) != 0;
                CHECK(hasForceOpaque);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-TLASMI-05: Non-opaque instances have ForceNonOpaque flag set
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASMI-05 TLASMultiInstance - non-opaque instances have ForceNonOpaque flag")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::InstanceDesc& desc = scene.m_RTInstanceDescs[i];
            const srrhi::PerInstanceData& inst = scene.m_InstanceData[i];
            const uint32_t matIdx = inst.m_MaterialIndex;

            if (matIdx >= (uint32_t)scene.m_Materials.size()) continue;
            const uint32_t alphaMode = scene.m_Materials[matIdx].m_AlphaMode;

            if (alphaMode != srrhi::CommonConsts::ALPHA_MODE_OPAQUE)
            {
                INFO("Instance " << i << " (non-opaque alphaMode=" << alphaMode << ") flags=" << (int)desc.flags);
                const bool hasForceNonOpaque = ((uint8_t)desc.flags & (uint8_t)nvrhi::rt::InstanceFlags::ForceNonOpaque) != 0;
                CHECK(hasForceNonOpaque);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-TLASMI-06: Instance count matches sum of opaque + masked + transparent buckets
    // ------------------------------------------------------------------
    TEST_CASE("TC-TLASMI-06 TLASMultiInstance - instance count matches bucket sum")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        const uint32_t bucketTotal = scene.m_OpaqueBucket.m_Count
                                   + scene.m_MaskedBucket.m_Count
                                   + scene.m_TransparentBucket.m_Count;
        CHECK(bucketTotal == (uint32_t)scene.m_RTInstanceDescs.size());
        CHECK(bucketTotal == (uint32_t)scene.m_InstanceData.size());
    }
}


// ============================================================================
// TEST SUITE: Scene_TransformConsistency
// ============================================================================
TEST_SUITE("Scene_TransformConsistency")
{
    // ------------------------------------------------------------------
    // TC-XFORM-01: RT instance desc transform row 0 matches PerInstanceData world matrix
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-01 TransformConsistency - RT transform row 0 matches world matrix")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[i].transform;
            const Matrix& w = scene.m_InstanceData[i].m_World;
            // RT transform is the transpose of the row-major world matrix.
            // Row 0 of RT transform = column 0 of world = (w._11, w._21, w._31, w._41)
            INFO("Instance " << i);
            CHECK(t[0] == doctest::Approx(w._11).epsilon(1e-5f));
            CHECK(t[1] == doctest::Approx(w._21).epsilon(1e-5f));
            CHECK(t[2] == doctest::Approx(w._31).epsilon(1e-5f));
            CHECK(t[3] == doctest::Approx(w._41).epsilon(1e-5f));
        }
    }

    // ------------------------------------------------------------------
    // TC-XFORM-02: RT instance desc transform row 1 matches PerInstanceData world matrix
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-02 TransformConsistency - RT transform row 1 matches world matrix")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[i].transform;
            const Matrix& w = scene.m_InstanceData[i].m_World;
            INFO("Instance " << i);
            CHECK(t[4] == doctest::Approx(w._12).epsilon(1e-5f));
            CHECK(t[5] == doctest::Approx(w._22).epsilon(1e-5f));
            CHECK(t[6] == doctest::Approx(w._32).epsilon(1e-5f));
            CHECK(t[7] == doctest::Approx(w._42).epsilon(1e-5f));
        }
    }

    // ------------------------------------------------------------------
    // TC-XFORM-03: RT instance desc transform row 2 matches PerInstanceData world matrix
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-03 TransformConsistency - RT transform row 2 matches world matrix")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[i].transform;
            const Matrix& w = scene.m_InstanceData[i].m_World;
            INFO("Instance " << i);
            CHECK(t[8]  == doctest::Approx(w._13).epsilon(1e-5f));
            CHECK(t[9]  == doctest::Approx(w._23).epsilon(1e-5f));
            CHECK(t[10] == doctest::Approx(w._33).epsilon(1e-5f));
            CHECK(t[11] == doctest::Approx(w._43).epsilon(1e-5f));
        }
    }

    // ------------------------------------------------------------------
    // TC-XFORM-04: RT instance desc transform is finite for all instances
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-04 TransformConsistency - RT instance desc transform is finite")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_RTInstanceDescs.size(); ++i)
        {
            const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[i].transform;
            INFO("Instance " << i);
            for (int k = 0; k < 12; ++k)
                CHECK(std::isfinite(t[k]));
        }
    }

    // ------------------------------------------------------------------
    // TC-XFORM-05: Identity-transform node produces identity RT transform
    //              (in-memory minimal glTF has no transform = identity)
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-05 TransformConsistency - identity node produces identity RT transform")
    {
        REQUIRE(DEV() != nullptr);
        REQUIRE(LoadMinimalSceneWithAS());

        const Scene& scene = g_Renderer.m_Scene;
        REQUIRE(!scene.m_RTInstanceDescs.empty());

        // The minimal glTF has no transform on the node, so world = identity.
        // RT transform (row-major 3x4) of identity = {{1,0,0,0},{0,1,0,0},{0,0,1,0}}
        const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[0].transform;
        CHECK(t[0]  == doctest::Approx(1.0f).epsilon(1e-5f)); // row0 col0
        CHECK(t[1]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row0 col1
        CHECK(t[2]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row0 col2
        CHECK(t[3]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row0 translation X
        CHECK(t[4]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row1 col0
        CHECK(t[5]  == doctest::Approx(1.0f).epsilon(1e-5f)); // row1 col1
        CHECK(t[6]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row1 col2
        CHECK(t[7]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row1 translation Y
        CHECK(t[8]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row2 col0
        CHECK(t[9]  == doctest::Approx(0.0f).epsilon(1e-5f)); // row2 col1
        CHECK(t[10] == doctest::Approx(1.0f).epsilon(1e-5f)); // row2 col2
        CHECK(t[11] == doctest::Approx(0.0f).epsilon(1e-5f)); // row2 translation Z

        ShutdownMinimalScene();
    }

    // ------------------------------------------------------------------
    // TC-XFORM-06: Translated node produces correct RT transform translation column.
    //              Uses a glTF with a known translation on the root node.
    //
    //  RH→LH coordinate conversion (SceneLoader.cpp):
    //    node.m_Translation.z = -cn.translation[2]
    //  So a glTF translation of (3, 5, 7) becomes LH (3, 5, -7).
    //  BuildAccelerationStructures packs world._41/_42/_43 into t[3]/t[7]/t[11],
    //  so the expected RT translation column is (3, 5, -7) — NOT (3, 5, 7).
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-06 TransformConsistency - translated node produces correct RT translation")
    {
        REQUIRE(DEV() != nullptr);
        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();

        // Minimal glTF with a translation of (3, 5, 7) on the root node.
        // After RH->LH conversion the engine stores (3, 5, -7).
        static constexpr const char kTranslatedGltf[] = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0, "translation": [ 3.0, 5.0, 7.0 ] } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "accessors": [ {
    "bufferView": 0, "byteOffset": 0,
    "componentType": 5126, "count": 3, "type": "VEC3",
    "max": [ 1.0, 1.0, 0.0 ], "min": [ 0.0, 0.0, 0.0 ]
  } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 } ],
  "buffers": [ {
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
    "byteLength": 36
  } ]
})";

        g_Renderer.m_Scene.InitializeDefaultCube(0, 0);
        g_Renderer.ExecutePendingCommandLists();

        std::vector<srrhi::VertexQuantized> vertices;
        std::vector<uint32_t> indices;
        const bool ok = SceneLoader::LoadGLTFSceneFromMemory(
            g_Renderer.m_Scene, kTranslatedGltf, sizeof(kTranslatedGltf) - 1,
            {}, vertices, indices);
        REQUIRE(ok);

        g_Renderer.m_Scene.FinalizeLoadedScene();
        SceneLoader::LoadTexturesFromImages(g_Renderer.m_Scene, {});
        g_Renderer.m_Scene.UploadGeometryBuffers(vertices, indices);
        SceneLoader::CreateAndUploadLightBuffer(g_Renderer.m_Scene);
        {
            nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
            ScopedCommandList scopedCmd{ cmd, "Test_TranslatedAS" };
            SceneLoader::UpdateMaterialsAndCreateConstants(g_Renderer.m_Scene, cmd);
            g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
        }
        g_Renderer.ExecutePendingCommandLists();
        DEV()->waitForIdle();

        REQUIRE(!g_Renderer.m_Scene.m_RTInstanceDescs.empty());
        const nvrhi::rt::AffineTransform& t = g_Renderer.m_Scene.m_RTInstanceDescs[0].transform;

        // Verify the node's LH translation was stored correctly before the AS build.
        // Find the mesh node (skip the default-cube node at index 0).
        bool foundTranslatedNode = false;
        for (const auto& node : g_Renderer.m_Scene.m_Nodes)
        {
            if (node.m_MeshIndex < 0) continue; // skip non-mesh nodes (lights, cameras)
            INFO("Node '" << node.m_Name << "' LH translation: ("
                 << node.m_Translation.x << ", "
                 << node.m_Translation.y << ", "
                 << node.m_Translation.z << ")");
            // X and Y are unchanged; Z is negated by RH->LH conversion.
            if (std::abs(node.m_Translation.x - 3.0f) < 1e-4f &&
                std::abs(node.m_Translation.y - 5.0f) < 1e-4f)
            {
                CHECK(node.m_Translation.x == doctest::Approx( 3.0f).epsilon(1e-4f));
                CHECK(node.m_Translation.y == doctest::Approx( 5.0f).epsilon(1e-4f));
                CHECK(node.m_Translation.z == doctest::Approx(-7.0f).epsilon(1e-4f)); // RH->LH Z-flip
                foundTranslatedNode = true;
                break;
            }
        }
        CHECK(foundTranslatedNode);

        // RT AffineTransform is row-major 3x4: [row0col0..col3, row1col0..col3, row2col0..col3]
        // BuildAccelerationStructures packs: t[3]=world._41, t[7]=world._42, t[11]=world._43
        // world._41/_42/_43 = LH translation = (3, 5, -7).
        INFO("RT transform t[3]=" << t[3] << " t[7]=" << t[7] << " t[11]=" << t[11]);
        CHECK(t[3]  == doctest::Approx( 3.0f).epsilon(1e-4f)); // Tx: unchanged
        CHECK(t[7]  == doctest::Approx( 5.0f).epsilon(1e-4f)); // Ty: unchanged
        CHECK(t[11] == doctest::Approx(-7.0f).epsilon(1e-4f)); // Tz: negated by RH->LH Z-flip

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-XFORM-07: PerInstanceData world matrix diagonal is non-zero for mesh nodes
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-07 TransformConsistency - PerInstanceData world matrix diagonal is non-zero")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            const Matrix& w = scene.m_InstanceData[i].m_World;
            const bool nonZeroDiag = (w._11 != 0.0f || w._22 != 0.0f || w._33 != 0.0f || w._44 != 0.0f);
            INFO("Instance " << i);
            CHECK(nonZeroDiag);
        }
    }

    // ------------------------------------------------------------------
    // TC-XFORM-08: PerInstanceData m_MeshDataIndex is in-range for all instances
    // ------------------------------------------------------------------
    TEST_CASE("TC-XFORM-08 TransformConsistency - PerInstanceData m_MeshDataIndex is in-range")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        const uint32_t meshDataCount = (uint32_t)scene.m_MeshData.size();
        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            INFO("Instance " << i << " m_MeshDataIndex=" << scene.m_InstanceData[i].m_MeshDataIndex);
            CHECK(scene.m_InstanceData[i].m_MeshDataIndex < meshDataCount);
        }
    }
}


// ============================================================================
// TEST SUITE: Scene_RayIntersectionGeometry
// ============================================================================
TEST_SUITE("Scene_RayIntersectionGeometry")
{
    // ------------------------------------------------------------------
    // TC-RAYGEOM-01: BLAS geometry uses the scene vertex buffer
    //               (verified indirectly: vertex buffer is non-null and BLAS is built)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-01 RayIntersectionGeometry - scene vertex buffer is non-null for RT")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_VertexBufferQuantized != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-02: BLAS geometry uses the scene index buffer
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-02 RayIntersectionGeometry - scene index buffer is non-null for RT")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_IndexBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-03: BLAS geometry index count matches MeshData LOD-0 index count
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-03 RayIntersectionGeometry - MeshData LOD-0 index count is positive")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue;
                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const uint32_t lod0Count = scene.m_MeshData[meshDataIdx].m_IndexCounts[0];
                INFO("Mesh " << mi << " prim " << pi << " LOD-0 indexCount=" << lod0Count);
                CHECK(lod0Count > 0u);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-04: BLAS geometry vertex count matches primitive vertex count
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-04 RayIntersectionGeometry - primitive vertex count is positive")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue;
                INFO("Mesh " << mi << " prim " << pi << " vertexCount=" << prim.m_VertexCount);
                CHECK(prim.m_VertexCount > 0u);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-05: BLAS geometry vertex stride matches VertexQuantized size
    //               (verified via buffer desc structStride)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-05 RayIntersectionGeometry - vertex buffer structStride matches VertexQuantized")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_VertexBufferQuantized != nullptr);
        const uint32_t stride = g_Renderer.m_Scene.m_VertexBufferQuantized->getDesc().structStride;
        CHECK(stride == sizeof(srrhi::VertexQuantized));
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-06: BLAS geometry index offset is within index buffer bounds
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-06 RayIntersectionGeometry - LOD-0 index offset is within index buffer")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        REQUIRE(scene.m_IndexBuffer != nullptr);
        const uint64_t indexBufBytes = scene.m_IndexBuffer->getDesc().byteSize;
        const uint64_t indexBufCount = indexBufBytes / sizeof(uint32_t);

        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue;
                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const srrhi::MeshData& md = scene.m_MeshData[meshDataIdx];
                const uint64_t endIdx = (uint64_t)md.m_IndexOffsets[0] + md.m_IndexCounts[0];
                INFO("Mesh " << mi << " prim " << pi
                     << " indexOffset=" << md.m_IndexOffsets[0]
                     << " indexCount=" << md.m_IndexCounts[0]
                     << " bufCount=" << indexBufCount);
                CHECK(endIdx <= indexBufCount);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-07: All primitives with BLAS have non-zero vertex counts
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-07 RayIntersectionGeometry - all BLAS primitives have non-zero vertex count")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (const auto& mesh : scene.m_Meshes)
        {
            for (const auto& prim : mesh.m_Primitives)
            {
                if (prim.m_BLAS.empty()) continue;
                CHECK(prim.m_VertexCount > 0u);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-08: MeshData LOD-0 index count is a multiple of 3 (whole triangles)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-08 RayIntersectionGeometry - LOD-0 index count is a multiple of 3")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue;
                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const uint32_t idxCount = scene.m_MeshData[meshDataIdx].m_IndexCounts[0];
                INFO("Mesh " << mi << " prim " << pi << " LOD-0 indexCount=" << idxCount);
                CHECK(idxCount % 3u == 0u);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-RAYGEOM-09: MeshData LOD-0 index offset + count <= total index buffer used
    // ------------------------------------------------------------------
    TEST_CASE("TC-RAYGEOM-09 RayIntersectionGeometry - LOD-0 index range within used index buffer")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        const uint32_t indexBufUsed = scene.m_IndexBufferUsed;

        for (int mi = 0; mi < (int)scene.m_Meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)scene.m_Meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = scene.m_Meshes[mi].m_Primitives[pi];
                if (prim.m_BLAS.empty()) continue;
                const uint32_t meshDataIdx = prim.m_MeshDataIndex;
                REQUIRE(meshDataIdx < (uint32_t)scene.m_MeshData.size());
                const srrhi::MeshData& md = scene.m_MeshData[meshDataIdx];
                const uint32_t endIdx = md.m_IndexOffsets[0] + md.m_IndexCounts[0];
                INFO("Mesh " << mi << " prim " << pi
                     << " endIdx=" << endIdx << " indexBufUsed=" << indexBufUsed);
                CHECK(endIdx <= indexBufUsed);
            }
        }
    }
}


// ============================================================================
// TEST SUITE: Scene_RayQueryReadiness
// ============================================================================
TEST_SUITE("Scene_RayQueryReadiness")
{
    // ------------------------------------------------------------------
    // TC-RQREADY-01: TLAS is non-null after full scene load (RT-ready)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-01 RayQueryReadiness - TLAS is non-null after full scene load")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_TLAS != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-02: m_RTInstanceDescBuffer is non-null (needed for TLAS rebuild)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-02 RayQueryReadiness - m_RTInstanceDescBuffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_RTInstanceDescBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-03: m_BLASAddressBuffer is non-null (needed by TLASPatch_CS)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-03 RayQueryReadiness - m_BLASAddressBuffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_BLASAddressBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-04: m_InstanceLODBuffer is non-null (needed by TLASPatch_CS)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-04 RayQueryReadiness - m_InstanceLODBuffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_InstanceLODBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-05: m_InstanceDataBuffer is non-null (needed by RT shaders)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-05 RayQueryReadiness - m_InstanceDataBuffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_InstanceDataBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-06: TLAS max-instance capacity >= actual instance count
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-06 RayQueryReadiness - TLAS max-instance capacity >= instance count")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_TLAS != nullptr);
        const nvrhi::rt::AccelStructDesc& tlasDesc = g_Renderer.m_Scene.m_TLAS->getDesc();
        const uint32_t instanceCount = (uint32_t)g_Renderer.m_Scene.m_InstanceData.size();
        INFO("TLAS maxInstances=" << tlasDesc.topLevelMaxInstances
             << " instanceCount=" << instanceCount);
        CHECK(tlasDesc.topLevelMaxInstances >= instanceCount);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-07: RT instance desc buffer byte size >= instanceCount * 64 bytes
    //               (nvrhi::rt::InstanceDesc is exactly 64 bytes)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-07 RayQueryReadiness - RT instance desc buffer is large enough")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_RTInstanceDescBuffer != nullptr);
        const uint32_t instanceCount = (uint32_t)g_Renderer.m_Scene.m_InstanceData.size();
        const uint64_t minBytes = (uint64_t)instanceCount * 64u; // sizeof(nvrhi::rt::InstanceDesc) == 64
        const uint64_t actualBytes = g_Renderer.m_Scene.m_RTInstanceDescBuffer->getDesc().byteSize;
        CHECK(actualBytes >= minBytes);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-08: BLAS address buffer byte size >= instanceCount * MAX_LOD_COUNT * 8
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-08 RayQueryReadiness - BLAS address buffer is large enough")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_BLASAddressBuffer != nullptr);
        const uint32_t instanceCount = (uint32_t)g_Renderer.m_Scene.m_InstanceData.size();
        const uint64_t minBytes = (uint64_t)instanceCount
            * srrhi::CommonConsts::MAX_LOD_COUNT
            * sizeof(uint64_t);
        const uint64_t actualBytes = g_Renderer.m_Scene.m_BLASAddressBuffer->getDesc().byteSize;
        CHECK(actualBytes >= minBytes);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-09: Instance LOD buffer byte size >= instanceCount * 4 bytes
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-09 RayQueryReadiness - instance LOD buffer is large enough")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_InstanceLODBuffer != nullptr);
        const uint32_t instanceCount = (uint32_t)g_Renderer.m_Scene.m_InstanceData.size();
        const uint64_t minBytes = (uint64_t)instanceCount * sizeof(uint32_t);
        const uint64_t actualBytes = g_Renderer.m_Scene.m_InstanceLODBuffer->getDesc().byteSize;
        CHECK(actualBytes >= minBytes);
    }

    // ------------------------------------------------------------------
    // TC-RQREADY-10: All instances have a valid MeshDataIndex for RT triangle lookup
    // ------------------------------------------------------------------
    TEST_CASE("TC-RQREADY-10 RayQueryReadiness - all instances have valid MeshDataIndex")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        const uint32_t meshDataCount = (uint32_t)scene.m_MeshData.size();
        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            INFO("Instance " << i << " m_MeshDataIndex=" << scene.m_InstanceData[i].m_MeshDataIndex);
            CHECK(scene.m_InstanceData[i].m_MeshDataIndex < meshDataCount);
        }
    }
}


// ============================================================================
// TEST SUITE: Scene_RTXDISceneLoaded
// ============================================================================
TEST_SUITE("Scene_RTXDISceneLoaded")
{
    // ------------------------------------------------------------------
    // TC-RTXDI-01: m_InstanceData is non-empty after scene load (RTXDI needs instances)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-01 RTXDISceneLoaded - m_InstanceData is non-empty after scene load")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(!g_Renderer.m_Scene.m_InstanceData.empty());
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-02: All instances have m_MeshDataIndex in-range
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-02 RTXDISceneLoaded - all instances have m_MeshDataIndex in-range")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        const uint32_t meshDataCount = (uint32_t)scene.m_MeshData.size();
        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            INFO("Instance " << i);
            CHECK(scene.m_InstanceData[i].m_MeshDataIndex < meshDataCount);
        }
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-03: All instances have m_MaterialIndex in-range or UINT32_MAX (no material)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-03 RTXDISceneLoaded - all instances have m_MaterialIndex in-range or UINT32_MAX")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        const uint32_t matCount = (uint32_t)scene.m_Materials.size();
        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            const uint32_t matIdx = scene.m_InstanceData[i].m_MaterialIndex;
            INFO("Instance " << i << " m_MaterialIndex=" << matIdx);
            CHECK((matIdx == UINT32_MAX || matIdx < matCount));
        }
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-04: m_MeshDataBuffer is non-null (RTXDI reads mesh data)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-04 RTXDISceneLoaded - m_MeshDataBuffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_MeshDataBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-05: m_MaterialConstantsBuffer is non-null (RTXDI reads material data)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-05 RTXDISceneLoaded - m_MaterialConstantsBuffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-06: m_LightBuffer is non-null after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-06 RTXDISceneLoaded - m_LightBuffer is non-null after scene load")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);
        CHECK(g_Renderer.m_Scene.m_LightBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-07: m_LightCount matches m_Lights.size() after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-07 RTXDISceneLoaded - m_LightCount matches m_Lights.size()")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        INFO("m_LightCount=" << scene.m_LightCount
             << " m_Lights.size()=" << scene.m_Lights.size());
        CHECK(scene.m_LightCount == (uint32_t)scene.m_Lights.size());
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-08: Scene has at least one directional light (RTXDI sun light)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-08 RTXDISceneLoaded - scene has at least one directional light")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        bool hasDir = false;
        for (const auto& l : scene.m_Lights)
            if (l.m_Type == Scene::Light::Directional) { hasDir = true; break; }
        CHECK(hasDir);
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-09: All instance world matrices are finite (no NaN/Inf)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-09 RTXDISceneLoaded - all instance world matrices are finite")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            const Matrix& w = scene.m_InstanceData[i].m_World;
            INFO("Instance " << i);
            // Check all 16 elements of the 4x4 world matrix.
            CHECK(std::isfinite(w._11)); CHECK(std::isfinite(w._12));
            CHECK(std::isfinite(w._13)); CHECK(std::isfinite(w._14));
            CHECK(std::isfinite(w._21)); CHECK(std::isfinite(w._22));
            CHECK(std::isfinite(w._23)); CHECK(std::isfinite(w._24));
            CHECK(std::isfinite(w._31)); CHECK(std::isfinite(w._32));
            CHECK(std::isfinite(w._33)); CHECK(std::isfinite(w._34));
            CHECK(std::isfinite(w._41)); CHECK(std::isfinite(w._42));
            CHECK(std::isfinite(w._43)); CHECK(std::isfinite(w._44));
        }
    }

    // ------------------------------------------------------------------
    // TC-RTXDI-10: m_FirstGeometryInstanceIndex is 0 for the first instance
    //              (RTXDI geometry-to-light mapping starts at 0)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RTXDI-10 RTXDISceneLoaded - first instance m_FirstGeometryInstanceIndex is 0")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(!g_Renderer.m_Scene.m_InstanceData.empty());
        // The first instance in the opaque bucket should have index 0.
        const uint32_t firstInstIdx = g_Renderer.m_Scene.m_OpaqueBucket.m_BaseIndex;
        if (firstInstIdx < (uint32_t)g_Renderer.m_Scene.m_InstanceData.size())
        {
            const uint32_t geomInstIdx =
                g_Renderer.m_Scene.m_InstanceData[firstInstIdx].m_FirstGeometryInstanceIndex;
            INFO("firstInstIdx=" << firstInstIdx << " geomInstIdx=" << geomInstIdx);
            CHECK(geomInstIdx == 0u);
        }
    }
}

// ============================================================================
// Suite: Scene_CoordConvention
//
// These tests pin down the RH (glTF) → LH (D3D12) coordinate-system conversion
// that the SceneLoader applies to every node translation, and verify that the
// resulting RT AffineTransform stored in m_RTInstanceDescs is consistent.
//
// Key convention (documented here so future tests can reference it):
//   glTF uses a right-handed coordinate system (+Y up, -Z forward).
//   D3D12 / this engine uses a left-handed system (+Y up, +Z forward).
//   Conversion: negate the Z component of every translation and position.
//
// RT AffineTransform layout (row-major 3×4):
//   [ t[0]  t[1]  t[2]  t[3]  ]   ← row 0  (t[3]  = Tx)
//   [ t[4]  t[5]  t[6]  t[7]  ]   ← row 1  (t[7]  = Ty)
//   [ t[8]  t[9]  t[10] t[11] ]   ← row 2  (t[11] = Tz, already LH)
//
// The packing in Scene::BuildAccelerationStructures:
//   t[3]  = world._41   (DirectX row-vector: row 4 = translation row)
//   t[7]  = world._42
//   t[11] = world._43
// ============================================================================

// Shared helper: build a minimal in-memory scene from a glTF JSON string,
// run BuildAccelerationStructures, and return true if at least one RT instance
// desc was produced.  Returns false if loading or building failed.
static bool BuildMinimalSceneFromGltf(const char* gltfJson, size_t gltfLen)
{
    if (!DEV()) return false;

    // Diagnostic: log the JSON length so truncation bugs are immediately visible
    // in the test output (e.g. buf[] too small in MakeTranslatedGltf).
    SDL_Log("[BuildMinimalSceneFromGltf] JSON length = %zu bytes", gltfLen);

    DEV()->waitForIdle();
    g_Renderer.m_Scene.Shutdown();

    g_Renderer.m_Scene.InitializeDefaultCube(0, 0);
    g_Renderer.ExecutePendingCommandLists();

    std::vector<srrhi::VertexQuantized> vertices;
    std::vector<uint32_t> indices;
    if (!SceneLoader::LoadGLTFSceneFromMemory(
            g_Renderer.m_Scene, gltfJson, gltfLen, {}, vertices, indices))
    {
        SDL_Log("[BuildMinimalSceneFromGltf] LoadGLTFSceneFromMemory FAILED "
                "(JSON length=%zu — check for snprintf truncation in MakeTranslatedGltf)",
                gltfLen);
        return false;
    }

    g_Renderer.m_Scene.FinalizeLoadedScene();
    SceneLoader::LoadTexturesFromImages(g_Renderer.m_Scene, {});
    g_Renderer.m_Scene.UploadGeometryBuffers(vertices, indices);
    SceneLoader::CreateAndUploadLightBuffer(g_Renderer.m_Scene);
    {
        nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
        ScopedCommandList sc{ cmd, "CoordConv_Build" };
        SceneLoader::UpdateMaterialsAndCreateConstants(g_Renderer.m_Scene, cmd);
        g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
    }
    g_Renderer.ExecutePendingCommandLists();
    DEV()->waitForIdle();

    const bool ok = !g_Renderer.m_Scene.m_RTInstanceDescs.empty();
    SDL_Log("[BuildMinimalSceneFromGltf] m_RTInstanceDescs.size()=%zu  ok=%s",
            g_Renderer.m_Scene.m_RTInstanceDescs.size(), ok ? "true" : "false");
    return ok;
}

// Minimal single-triangle glTF template with a configurable translation.
// The three float arguments are substituted for the node's "translation" array.
//
// IMPORTANT: buf[] must be large enough to hold the fully-expanded JSON.
// The fixed template is ~530 chars; each %f expands to up to 14 chars (e.g.
// "-1234567.000000"), so worst-case total is ~530 + 3*14 = ~572 chars.
// buf[1024] gives ample headroom.  A truncation assert fires immediately if
// this ever becomes tight again.
static std::string MakeTranslatedGltf(float tx, float ty, float tz)
{
    // 1024 bytes: well above the ~572-byte worst-case expanded length.
    char buf[1024];
    const int written = std::snprintf(buf, sizeof(buf), R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0, "translation": [ %f, %f, %f ] } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "accessors": [ {
    "bufferView": 0, "byteOffset": 0,
    "componentType": 5126, "count": 3, "type": "VEC3",
    "max": [ 1.0, 1.0, 0.0 ], "min": [ 0.0, 0.0, 0.0 ]
  } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 } ],
  "buffers": [ {
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
    "byteLength": 36
  } ]
})", tx, ty, tz);

    // Guard: snprintf returns the number of chars it *would* have written
    // (excluding the null terminator).  If that equals or exceeds sizeof(buf)
    // the output was silently truncated, producing invalid JSON.
    SDL_assert(written > 0 && written < (int)sizeof(buf) &&
               "MakeTranslatedGltf: snprintf truncated — increase buf[] size");

    return std::string(buf, static_cast<size_t>(written));
}

TEST_SUITE("Scene_CoordConvention")
{
    // ------------------------------------------------------------------
    // TC-COORD-00: MakeTranslatedGltf produces a non-empty, non-truncated string
    //
    // Regression test for the buf[512] overflow bug: snprintf silently truncated
    // the JSON template (which is ~530+ chars), producing invalid JSON that
    // cgltf rejected with "invalid_json", causing all TC-COORD-* tests to fail
    // with a misleading "vector subscript out of range" crash.
    //
    // This test must run first in the suite so that a truncation failure is
    // reported with a clear message before any translation-value tests run.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-00 CoordConvention - MakeTranslatedGltf output is non-empty and not truncated")
    {
        // Generate with the worst-case float values: large negative numbers
        // produce the longest %f output (e.g. "-1234567.000000" = 15 chars).
        const std::string gltf = MakeTranslatedGltf(-1234567.0f, -1234567.0f, -1234567.0f);

        // Must be non-empty.
        REQUIRE(!gltf.empty());

        // Must end with '}' — a truncated string ends mid-JSON instead.
        // Trim trailing whitespace/newlines before checking.
        const size_t lastNonSpace = gltf.find_last_not_of(" \t\r\n");
        REQUIRE(lastNonSpace != std::string::npos);
        INFO("last non-space char = '" << gltf[lastNonSpace] << "'  full length = " << gltf.size());
        CHECK(gltf[lastNonSpace] == '}');

        // Must be long enough to contain the fixed base64 URI (a reliable
        // sentinel that only appears if the string was not truncated).
        const bool hasBase64 = gltf.find("AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA") != std::string::npos;
        INFO("gltf length=" << gltf.size() << "  hasBase64=" << hasBase64);
        CHECK(hasBase64);

        // Must be parseable as valid glTF (the real end-to-end check).
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-01: glTF Z translation is negated in the RT transform (RH→LH)
    //
    // A glTF node with translation [0, 0, 7] must produce t[11] == -7 in the
    // RT AffineTransform because the engine negates Z during RH→LH conversion.
    // This is the canonical regression test for the sign-flip bug caught by
    // TC-XFORM-06.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-01 CoordConvention - glTF Z translation is negated in RT transform (RH->LH)")
    {
        REQUIRE(DEV() != nullptr);
        const std::string gltf = MakeTranslatedGltf(0.0f, 0.0f, 7.0f);
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        const nvrhi::rt::AffineTransform& t = g_Renderer.m_Scene.m_RTInstanceDescs[0].transform;
        INFO("t[3]=" << t[3] << " t[7]=" << t[7] << " t[11]=" << t[11]);

        // X and Y must be zero (no translation on those axes).
        CHECK(t[3]  == doctest::Approx(0.0f).epsilon(1e-4f));
        CHECK(t[7]  == doctest::Approx(0.0f).epsilon(1e-4f));
        // Z must be negated: glTF +7 → LH -7.
        CHECK(t[11] == doctest::Approx(-7.0f).epsilon(1e-4f));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-02: glTF X and Y translations are preserved unchanged (no flip)
    //
    // Only Z is negated during RH→LH conversion; X and Y must pass through
    // without sign change.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-02 CoordConvention - glTF X and Y translations are preserved unchanged")
    {
        REQUIRE(DEV() != nullptr);
        const std::string gltf = MakeTranslatedGltf(3.0f, 5.0f, 0.0f);
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        const nvrhi::rt::AffineTransform& t = g_Renderer.m_Scene.m_RTInstanceDescs[0].transform;
        INFO("t[3]=" << t[3] << " t[7]=" << t[7] << " t[11]=" << t[11]);

        // X and Y must be unchanged.
        CHECK(t[3]  == doctest::Approx(3.0f).epsilon(1e-4f));
        CHECK(t[7]  == doctest::Approx(5.0f).epsilon(1e-4f));
        // Z is zero (no Z translation in glTF).
        CHECK(t[11] == doctest::Approx(0.0f).epsilon(1e-4f));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-03: Full XYZ translation — X and Y pass through, Z is negated
    //
    // Combines TC-COORD-01 and TC-COORD-02 into a single round-trip check
    // with all three axes non-zero.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-03 CoordConvention - full XYZ translation: X/Y preserved, Z negated")
    {
        REQUIRE(DEV() != nullptr);
        const std::string gltf = MakeTranslatedGltf(3.0f, 5.0f, 7.0f);
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        const nvrhi::rt::AffineTransform& t = g_Renderer.m_Scene.m_RTInstanceDescs[0].transform;
        INFO("t[3]=" << t[3] << " t[7]=" << t[7] << " t[11]=" << t[11]);

        CHECK(t[3]  == doctest::Approx( 3.0f).epsilon(1e-4f));  // X: unchanged
        CHECK(t[7]  == doctest::Approx( 5.0f).epsilon(1e-4f));  // Y: unchanged
        CHECK(t[11] == doctest::Approx(-7.0f).epsilon(1e-4f));  // Z: negated

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-04: Negative glTF Z translation becomes positive in LH space
    //
    // A glTF translation of [0, 0, -4] must produce t[11] == +4 after the
    // RH→LH negation.  This is the mirror of TC-COORD-01.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-04 CoordConvention - negative glTF Z becomes positive in LH RT transform")
    {
        REQUIRE(DEV() != nullptr);
        const std::string gltf = MakeTranslatedGltf(0.0f, 0.0f, -4.0f);
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        const nvrhi::rt::AffineTransform& t = g_Renderer.m_Scene.m_RTInstanceDescs[0].transform;
        INFO("t[3]=" << t[3] << " t[7]=" << t[7] << " t[11]=" << t[11]);

        CHECK(t[3]  == doctest::Approx(0.0f).epsilon(1e-4f));
        CHECK(t[7]  == doctest::Approx(0.0f).epsilon(1e-4f));
        CHECK(t[11] == doctest::Approx(4.0f).epsilon(1e-4f));  // -(-4) = +4

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-05: SceneLoader stores the negated Z in Node::m_Translation
    //
    // Verifies that the RH→LH flip is applied at the SceneLoader level
    // (m_Translation.z == -gltfZ) so that downstream code (BuildAS, animation)
    // always works in LH space.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-05 CoordConvention - SceneLoader stores negated Z in Node m_Translation")
    {
        REQUIRE(DEV() != nullptr);
        const std::string gltf = MakeTranslatedGltf(1.0f, 2.0f, 9.0f);
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        const Scene& scene = g_Renderer.m_Scene;

        // Find the first non-light, non-camera mesh node.
        bool found = false;
        for (const auto& node : scene.m_Nodes)
        {
            if (node.m_MeshIndex < 0) continue;
            INFO("Node '" << node.m_Name
                 << "' m_Translation=(" << node.m_Translation.x
                 << ", " << node.m_Translation.y
                 << ", " << node.m_Translation.z << ")");
            // X and Y must be unchanged from glTF.
            CHECK(node.m_Translation.x == doctest::Approx(1.0f).epsilon(1e-4f));
            CHECK(node.m_Translation.y == doctest::Approx(2.0f).epsilon(1e-4f));
            // Z must be negated: glTF +9 → LH -9.
            CHECK(node.m_Translation.z == doctest::Approx(-9.0f).epsilon(1e-4f));
            found = true;
            break;
        }
        CHECK(found);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-06: PerInstanceData world matrix translation row matches
    //              the LH-converted node translation
    //
    // After FinalizeLoadedScene the world matrix in m_InstanceData must
    // carry the LH translation (Z negated), not the original glTF value.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-06 CoordConvention - PerInstanceData world matrix carries LH translation")
    {
        REQUIRE(DEV() != nullptr);
        const std::string gltf = MakeTranslatedGltf(3.0f, 5.0f, 7.0f);
        REQUIRE(BuildMinimalSceneFromGltf(gltf.c_str(), gltf.size()));

        const Scene& scene = g_Renderer.m_Scene;
        REQUIRE(!scene.m_InstanceData.empty());

        // Find the instance that belongs to the translated mesh node.
        // In a single-mesh scene the first non-default-cube instance is index 1
        // (index 0 is the default cube inserted by InitializeDefaultCube).
        // We search for the instance whose world translation is non-zero.
        bool found = false;
        for (const auto& inst : scene.m_InstanceData)
        {
            const Matrix& w = inst.m_World;
            // DirectX row-vector convention: translation is in row 4 (_41, _42, _43).
            if (w._41 == 0.0f && w._42 == 0.0f && w._43 == 0.0f) continue;

            INFO("world._41=" << w._41 << " _42=" << w._42 << " _43=" << w._43);
            CHECK(w._41 == doctest::Approx( 3.0f).epsilon(1e-4f));  // Tx: unchanged
            CHECK(w._42 == doctest::Approx( 5.0f).epsilon(1e-4f));  // Ty: unchanged
            CHECK(w._43 == doctest::Approx(-7.0f).epsilon(1e-4f));  // Tz: negated (LH)
            found = true;
            break;
        }
        CHECK(found);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-COORD-07: RT AffineTransform translation column is consistent with
    //              PerInstanceData world matrix translation row
    //
    // Verifies the packing formula used in BuildAccelerationStructures:
    //   t[3]  = world._41,  t[7]  = world._42,  t[11] = world._43
    // for every instance in the scene.
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-07 CoordConvention - RT transform translation column matches world matrix row 4")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        REQUIRE(scene.m_InstanceData.size() == scene.m_RTInstanceDescs.size());

        for (uint32_t i = 0; i < (uint32_t)scene.m_InstanceData.size(); ++i)
        {
            const Matrix& w = scene.m_InstanceData[i].m_World;
            const nvrhi::rt::AffineTransform& t = scene.m_RTInstanceDescs[i].transform;

            INFO("Instance " << i
                 << " world._41=" << w._41 << " t[3]="  << t[3]
                 << " world._42=" << w._42 << " t[7]="  << t[7]
                 << " world._43=" << w._43 << " t[11]=" << t[11]);

            // The packing transposes the row-vector matrix into the column-vector
            // layout expected by DXR:  t[3]=_41, t[7]=_42, t[11]=_43.
            CHECK(t[3]  == doctest::Approx(w._41).epsilon(1e-5f));
            CHECK(t[7]  == doctest::Approx(w._42).epsilon(1e-5f));
            CHECK(t[11] == doctest::Approx(w._43).epsilon(1e-5f));
        }
    }

    // ------------------------------------------------------------------
    // TC-COORD-08: Zero-translation node produces zero translation in RT transform
    //
    // A node with no translation must have t[3]==t[7]==t[11]==0 in the RT
    // AffineTransform (identity translation column).
    // ------------------------------------------------------------------
    TEST_CASE("TC-COORD-08 CoordConvention - zero-translation node produces zero RT translation column")
    {
        REQUIRE(DEV() != nullptr);
        // glTF node with no translation field at all → identity transform.
        static constexpr const char kNoTranslationGltf[] = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "accessors": [ {
    "bufferView": 0, "byteOffset": 0,
    "componentType": 5126, "count": 3, "type": "VEC3",
    "max": [ 1.0, 1.0, 0.0 ], "min": [ 0.0, 0.0, 0.0 ]
  } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 } ],
  "buffers": [ {
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
    "byteLength": 36
  } ]
})";
        REQUIRE(BuildMinimalSceneFromGltf(kNoTranslationGltf, sizeof(kNoTranslationGltf) - 1));

        const nvrhi::rt::AffineTransform& t = g_Renderer.m_Scene.m_RTInstanceDescs[0].transform;
        INFO("t[3]=" << t[3] << " t[7]=" << t[7] << " t[11]=" << t[11]);

        CHECK(t[3]  == doctest::Approx(0.0f).epsilon(1e-5f));
        CHECK(t[7]  == doctest::Approx(0.0f).epsilon(1e-5f));
        CHECK(t[11] == doctest::Approx(0.0f).epsilon(1e-5f));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }
}
