// Tests_Scene.cpp - Phase 3: Scene Loading Tests
//
// Systems under test: SceneLoader, Scene, cgltf
// Prerequisites: g_Renderer fully initialized (RHI + CommonResources).
//                Scene loading is performed inside each test suite using
//                a helper that temporarily overrides Config::m_ScenePath.
//
// Test scenes used (from KhronosGroup/glTF-Sample-Assets, optional):
//   Models/BoxTextured/glTF/BoxTextured.gltf     - simple mesh + texture
//   Models/AnimatedCube/glTF/AnimatedCube.gltf   - animation channels
//   Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf - multiple meshes + nodes
//   Models/Lantern/glTF/Lantern.gltf             - 1 materials + textures
//
// When --gltf-samples <path> is NOT provided, all scene-dependent tests are
// skipped gracefully (SKIP_IF_NO_SAMPLES macro).
//
// Run with: HobbyRenderer --run-tests=*Scene* --gltf-samples <path>
// ============================================================================

#include "TestFixtures.h"

// ============================================================================
// TEST SUITE: Scene_NodeHierarchy
// ============================================================================
TEST_SUITE("Scene_NodeHierarchy")
{
    // ------------------------------------------------------------------
    // TC-NODE-01: Scene has at least one node after loading
    // ------------------------------------------------------------------
    TEST_CASE("TC-NODE-01 Nodes - scene has at least one node")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_Nodes.size() > 0);
    }

    // ------------------------------------------------------------------
    // TC-NODE-02: All node parent indices are valid (in-range or -1)
    // ------------------------------------------------------------------
    TEST_CASE("TC-NODE-02 Nodes - all parent indices are valid")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& nodes = g_Renderer.m_Scene.m_Nodes;
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            INFO("Node index: " << i << " name: " << nodes[i].m_Name);
            const int parent = nodes[i].m_Parent;
            CHECK((parent == -1 || (parent >= 0 && parent < (int)nodes.size())));
        }
    }

    // ------------------------------------------------------------------
    // TC-NODE-03: All node children indices are valid
    // ------------------------------------------------------------------
    TEST_CASE("TC-NODE-03 Nodes - all children indices are valid")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& nodes = g_Renderer.m_Scene.m_Nodes;
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            for (int childIdx : nodes[i].m_Children)
            {
                INFO("Node " << i << " child index: " << childIdx);
                CHECK((childIdx >= 0 && childIdx < (int)nodes.size()));
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-NODE-04: World transforms are non-zero for mesh nodes
    // ------------------------------------------------------------------
    TEST_CASE("TC-NODE-04 Nodes - world transforms are non-zero for mesh nodes")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& nodes = g_Renderer.m_Scene.m_Nodes;
        bool foundMeshNode = false;
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            if (nodes[i].m_MeshIndex < 0) continue;
            foundMeshNode = true;
            // The diagonal of the world transform should not be all zeros
            const Matrix& w = nodes[i].m_WorldTransform;
            const bool nonZero = (w._11 != 0.0f || w._22 != 0.0f || w._33 != 0.0f || w._44 != 0.0f);
            INFO("Node " << i << " (" << nodes[i].m_Name << ") world transform diagonal");
            CHECK(nonZero);
        }
        CHECK(foundMeshNode); // Sanity: the scene must have at least one mesh node
    }

    // ------------------------------------------------------------------
    // TC-NODE-05: Multi-node scene has correct parent-child linkage
    //             (CesiumMilkTruck has a root node with child nodes)
    // ------------------------------------------------------------------
    TEST_CASE("TC-NODE-05 Nodes - parent-child linkage is consistent")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const auto& nodes = g_Renderer.m_Scene.m_Nodes;
        // For every node that has a parent, the parent's children list must
        // contain this node's index.
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            const int parent = nodes[i].m_Parent;
            if (parent == -1) continue;

            const auto& parentChildren = nodes[parent].m_Children;
            const bool found = std::find(parentChildren.begin(), parentChildren.end(), i) != parentChildren.end();
            INFO("Node " << i << " (" << nodes[i].m_Name << ") parent=" << parent);
            CHECK(found);
        }
    }

    // ------------------------------------------------------------------
    // TC-NODE-06: Bounding sphere radius is non-negative for mesh nodes
    // ------------------------------------------------------------------
    TEST_CASE("TC-NODE-06 Nodes - bounding sphere radius is non-negative for mesh nodes")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& nodes = g_Renderer.m_Scene.m_Nodes;
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            if (nodes[i].m_MeshIndex < 0) continue;
            INFO("Node " << i << " (" << nodes[i].m_Name << ") radius=" << nodes[i].m_Radius);
            CHECK(nodes[i].m_Radius >= 0.0f);
        }
    }
}

// ============================================================================
// TEST SUITE: Scene_MeshData
// ============================================================================
TEST_SUITE("Scene_MeshData")
{
    // ------------------------------------------------------------------
    // TC-MESH-01: Scene has at least one mesh after loading
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-01 Meshes - scene has at least one mesh")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_Meshes.size() > 0);
    }

    // ------------------------------------------------------------------
    // TC-MESH-02: Every mesh has at least one primitive
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-02 Meshes - every mesh has at least one primitive")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& meshes = g_Renderer.m_Scene.m_Meshes;
        for (int i = 0; i < (int)meshes.size(); ++i)
        {
            INFO("Mesh index: " << i);
            CHECK(meshes[i].m_Primitives.size() > 0);
        }
    }

    // ------------------------------------------------------------------
    // TC-MESH-03: Every primitive has a valid material index
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-03 Meshes - every primitive has a valid material index")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& meshes    = g_Renderer.m_Scene.m_Meshes;
        const int   matCount  = (int)g_Renderer.m_Scene.m_Materials.size();
        int noMaterialPrimitiveCount = 0;
        for (int mi = 0; mi < (int)meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)meshes[mi].m_Primitives.size(); ++pi)
            {
                const Scene::Primitive& prim = meshes[mi].m_Primitives[pi];
                const int matIdx = prim.m_MaterialIndex;
                INFO("Mesh " << mi << " primitive " << pi
                    << " materialIndex=" << matIdx
                    << " meshDataIndex=" << prim.m_MeshDataIndex);

                if (matIdx == -1)
                {
                    ++noMaterialPrimitiveCount;
                    CHECK(prim.m_MeshDataIndex == 0u);
                }
                else
                {
                    CHECK((matIdx >= 0 && matIdx < matCount));
                }
            }
        }

        // The scene should contain at most one no-material primitive: the default cube placeholder.
        CHECK(noMaterialPrimitiveCount <= 1);
    }

    // ------------------------------------------------------------------
    // TC-MESH-04: Every primitive has a non-zero vertex count
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-04 Meshes - every primitive has a non-zero vertex count")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& meshes = g_Renderer.m_Scene.m_Meshes;
        for (int mi = 0; mi < (int)meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)meshes[mi].m_Primitives.size(); ++pi)
            {
                INFO("Mesh " << mi << " primitive " << pi);
                CHECK(meshes[mi].m_Primitives[pi].m_VertexCount > 0);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-MESH-05: GPU vertex buffer is non-null after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-05 Meshes - GPU vertex buffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_VertexBufferQuantized != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MESH-06: GPU index buffer is non-null after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-06 Meshes - GPU index buffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_IndexBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MESH-07: MeshData array is non-empty and consistent with meshes
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-07 Meshes - MeshData array is non-empty")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_MeshData.size() > 0);
        CHECK(g_Renderer.m_Scene.m_MeshDataBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MESH-08: Every primitive's MeshDataIndex is in-range
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-08 Meshes - every primitive MeshDataIndex is in-range")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& meshes       = g_Renderer.m_Scene.m_Meshes;
        const size_t meshDataSz  = g_Renderer.m_Scene.m_MeshData.size();
        for (int mi = 0; mi < (int)meshes.size(); ++mi)
        {
            for (int pi = 0; pi < (int)meshes[mi].m_Primitives.size(); ++pi)
            {
                const uint32_t idx = meshes[mi].m_Primitives[pi].m_MeshDataIndex;
                INFO("Mesh " << mi << " primitive " << pi << " MeshDataIndex=" << idx);
                CHECK(idx < (uint32_t)meshDataSz);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-MESH-09: Scene bounding sphere has a positive radius
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-09 Meshes - scene bounding sphere has positive radius")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_SceneBoundingSphere.Radius > 0.0f);
    }

    // ------------------------------------------------------------------
    // TC-MESH-10: Instance data is non-empty and buckets are consistent
    // ------------------------------------------------------------------
    TEST_CASE("TC-MESH-10 Meshes - instance data is non-empty and buckets are consistent")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Scene& scene = g_Renderer.m_Scene;
        CHECK(scene.m_InstanceData.size() > 0);
        CHECK(scene.m_InstanceDataBuffer != nullptr);

        // Total bucket counts must equal total instance count
        const uint32_t totalBuckets = scene.m_OpaqueBucket.m_Count
                                    + scene.m_MaskedBucket.m_Count
                                    + scene.m_TransparentBucket.m_Count;
        CHECK(totalBuckets == (uint32_t)scene.m_InstanceData.size());
    }
}

// ============================================================================
// TEST SUITE: Scene_Materials
// ============================================================================
TEST_SUITE("Scene_Materials")
{
    // ------------------------------------------------------------------
    // TC-MAT-01: Scene has at least one material after loading
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-01 Materials - scene has at least one material")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_Materials.size() > 0);
    }

    // ------------------------------------------------------------------
    // TC-MAT-02: Material constants GPU buffer is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-02 Materials - GPU material constants buffer is non-null")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MAT-03: All materials have valid alpha mode values
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-03 Materials - all materials have valid alpha mode")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ") alphaMode=" << mats[i].m_AlphaMode);
            // Valid values: OPAQUE=0, MASK=1, BLEND=2 (from srrhi::CommonConsts)
            CHECK(mats[i].m_AlphaMode <= 2u);
        }
    }

    // ------------------------------------------------------------------
    // TC-MAT-04: All materials have roughness and metallic factors in [0,1]
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-04 Materials - roughness and metallic factors are in [0,1]")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ")");
            CHECK(mats[i].m_RoughnessFactor >= 0.0f);
            CHECK(mats[i].m_RoughnessFactor <= 1.0f);
            CHECK(mats[i].m_MetallicFactor  >= 0.0f);
            CHECK(mats[i].m_MetallicFactor  <= 1.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-MAT-05: All materials have base color factor components in [0,1]
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-05 Materials - base color factor components are in [0,1]")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ")");
            const Vector4& c = mats[i].m_BaseColorFactor;
            CHECK(c.x >= 0.0f); CHECK(c.x <= 1.0f);
            CHECK(c.y >= 0.0f); CHECK(c.y <= 1.0f);
            CHECK(c.z >= 0.0f); CHECK(c.z <= 1.0f);
            CHECK(c.w >= 0.0f); CHECK(c.w <= 1.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-MAT-06: scene (Lantern) has one material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-06 Materials - scene (Lantern) has 1 material")
    {
        SKIP_IF_NO_SAMPLES("Lantern/glTF/Lantern.gltf");
        SceneScope scope("Lantern/glTF/Lantern.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_Materials.size() == 1);
    }

    // ------------------------------------------------------------------
    // TC-MAT-07: All material texture indices are in-range or -1
    // ------------------------------------------------------------------
    TEST_CASE("TC-MAT-07 Materials - texture indices are in-range or -1")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const int   texCount = (int)g_Renderer.m_Scene.m_Textures.size();
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ")");
            auto inRange = [&](int idx) { return idx == -1 || (idx >= 0 && idx < texCount); };
            CHECK(inRange(mats[i].m_BaseColorTexture));
            CHECK(inRange(mats[i].m_NormalTexture));
            CHECK(inRange(mats[i].m_MetallicRoughnessTexture));
            CHECK(inRange(mats[i].m_EmissiveTexture));
        }
    }
}

// ============================================================================
// TEST SUITE: Scene_Textures
// ============================================================================
TEST_SUITE("Scene_Textures")
{
    // ------------------------------------------------------------------
    // TC-TEX-SCENE-01: Textured scene has at least one texture
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-SCENE-01 SceneTextures - textured scene has at least one texture")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_Textures.size() > 0);
    }

    // ------------------------------------------------------------------
    // TC-TEX-SCENE-02: All loaded textures have a valid GPU handle
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-SCENE-02 SceneTextures - all textures have valid GPU handles")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)textures.size(); ++i)
        {
            INFO("Texture " << i << " uri=" << textures[i].m_Uri);
            CHECK(textures[i].m_Handle != nullptr);
        }
    }

    // ------------------------------------------------------------------
    // TC-TEX-SCENE-03: All loaded textures have a valid bindless index
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-SCENE-03 SceneTextures - all textures have valid bindless indices")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)textures.size(); ++i)
        {
            INFO("Texture " << i << " uri=" << textures[i].m_Uri);
            CHECK(textures[i].m_BindlessIndex != UINT32_MAX);
        }
    }

    // ------------------------------------------------------------------
    // TC-TEX-SCENE-04: All loaded textures have non-zero dimensions
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-SCENE-04 SceneTextures - all textures have non-zero dimensions")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)textures.size(); ++i)
        {
            REQUIRE(textures[i].m_Handle != nullptr);
            const nvrhi::TextureDesc& desc = textures[i].m_Handle->getDesc();
            INFO("Texture " << i << " uri=" << textures[i].m_Uri
                 << " " << desc.width << "x" << desc.height);
            CHECK(desc.width  > 0u);
            CHECK(desc.height > 0u);
        }
    }

    // ------------------------------------------------------------------
    // TC-TEX-SCENE-05: All loaded textures are Texture2D
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-SCENE-05 SceneTextures - all scene textures are Texture2D")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)textures.size(); ++i)
        {
            REQUIRE(textures[i].m_Handle != nullptr);
            const nvrhi::TextureDesc& desc = textures[i].m_Handle->getDesc();
            INFO("Texture " << i << " uri=" << textures[i].m_Uri);
            CHECK(desc.dimension == nvrhi::TextureDimension::Texture2D);
        }
    }

    // ------------------------------------------------------------------
    // TC-TEX-SCENE-06: Sampler type is either Wrap or Clamp (valid enum)
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-SCENE-06 SceneTextures - sampler type is Wrap or Clamp")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)textures.size(); ++i)
        {
            INFO("Texture " << i << " uri=" << textures[i].m_Uri);
            const auto s = textures[i].m_Sampler;
            CHECK((s == Scene::Texture::Wrap || s == Scene::Texture::Clamp));
        }
    }
}

// ============================================================================
// TEST SUITE: Scene_BoundingBoxes
// ============================================================================
TEST_SUITE("Scene_BoundingBoxes")
{
    // ------------------------------------------------------------------
    // TC-BBOX-01: All mesh bounding sphere radii are non-negative
    // ------------------------------------------------------------------
    TEST_CASE("TC-BBOX-01 BoundingBoxes - mesh bounding sphere radii are non-negative")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& meshes = g_Renderer.m_Scene.m_Meshes;
        for (int i = 0; i < (int)meshes.size(); ++i)
        {
            INFO("Mesh " << i << " radius=" << meshes[i].m_Radius);
            CHECK(meshes[i].m_Radius >= 0.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-BBOX-03: Instance bounding sphere radii are non-negative
    // ------------------------------------------------------------------
    TEST_CASE("TC-BBOX-03 BoundingBoxes - instance bounding sphere radii are non-negative")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& instances = g_Renderer.m_Scene.m_InstanceData;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            INFO("Instance " << i << " radius=" << instances[i].m_Radius);
            CHECK(instances[i].m_Radius >= 0.0f);
        }
    }
}

// NOTE: Scene_AccelStructures tests are in Tests_SceneAdvanced.cpp (Scene_AccelStructures suite).
// NOTE: Scene_Animations tests are in Tests_SceneAnimation.cpp (TC-AUPD-02 covers Update() advancing time).
// NOTE: Scene_Lights tests are in Tests_Lighting.cpp (Lighting_LightSystem suite).

// ============================================================================
// TEST SUITE: Scene_Cameras
// ============================================================================
TEST_SUITE("Scene_Cameras")
{
    // ------------------------------------------------------------------
    // TC-CAM-SCENE-01: Scene with a camera has at no camera
    // ------------------------------------------------------------------
    TEST_CASE("TC-CAM-SCENE-01 SceneCameras - scene with camera has at no camera")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        // CesiumMilkTruck includes a camera in the glTF
        CHECK(g_Renderer.m_Scene.m_Cameras.size() == 0);
    }

    // ------------------------------------------------------------------
    // TC-CAM-SCENE-02: All cameras have a valid node index
    // ------------------------------------------------------------------
    TEST_CASE("TC-CAM-SCENE-02 SceneCameras - all cameras have a valid node index")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const auto& cameras   = g_Renderer.m_Scene.m_Cameras;
        const int   nodeCount = (int)g_Renderer.m_Scene.m_Nodes.size();
        for (int i = 0; i < (int)cameras.size(); ++i)
        {
            INFO("Camera " << i << " (" << cameras[i].m_Name << ") nodeIndex=" << cameras[i].m_NodeIndex);
            CHECK((cameras[i].m_NodeIndex >= 0 && cameras[i].m_NodeIndex < nodeCount));
        }
    }

    // ------------------------------------------------------------------
    // TC-CAM-SCENE-03: All cameras have a positive field of view
    // ------------------------------------------------------------------
    TEST_CASE("TC-CAM-SCENE-03 SceneCameras - all cameras have positive FoV")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const auto& cameras = g_Renderer.m_Scene.m_Cameras;
        for (int i = 0; i < (int)cameras.size(); ++i)
        {
            INFO("Camera " << i << " (" << cameras[i].m_Name << ") fovY=" << cameras[i].m_Projection.fovY);
            CHECK(cameras[i].m_Projection.fovY > 0.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-CAM-SCENE-04: All cameras have positive near/far clip planes
    // ------------------------------------------------------------------
    TEST_CASE("TC-CAM-SCENE-04 SceneCameras - all cameras have positive near/far clip planes")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        const auto& cameras = g_Renderer.m_Scene.m_Cameras;
        for (int i = 0; i < (int)cameras.size(); ++i)
        {
            INFO("Camera " << i << " (" << cameras[i].m_Name << ")");
            CHECK(cameras[i].m_Projection.nearZ > 0.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-CAM-SCENE-05: When a scene camera is present, m_SelectedCameraIndex is valid
    // ------------------------------------------------------------------
    TEST_CASE("TC-CAM-SCENE-05 SceneCameras - SelectedCameraIndex is valid when cameras exist")
    {
        SKIP_IF_NO_SAMPLES("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        SceneScope scope("CesiumMilkTruck/glTF/CesiumMilkTruck.gltf");
        REQUIRE(scope.loaded);

        if (g_Renderer.m_Scene.m_Cameras.empty())
            return; // No cameras in this scene - skip

        const int sel = g_Renderer.m_Scene.m_SelectedCameraIndex;
        CHECK((sel >= 0 && sel < (int)g_Renderer.m_Scene.m_Cameras.size()));
    }
}

// ============================================================================
// TEST SUITE: Scene_SceneShutdown
// ============================================================================
TEST_SUITE("Scene_Shutdown")
{
    // ------------------------------------------------------------------
    // TC-SHUT-01: Scene::Shutdown() clears all CPU-side containers
    // ------------------------------------------------------------------
    TEST_CASE("TC-SHUT-01 Shutdown - Shutdown() clears all CPU-side containers")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");

        // Load the scene
        {
            SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
            REQUIRE(scope.loaded);
            // scope destructor calls Shutdown() and waitForIdle()
        }

        // After shutdown, all containers must be empty
        const Scene& scene = g_Renderer.m_Scene;
        CHECK(scene.m_Meshes.empty());
        CHECK(scene.m_Nodes.empty());
        CHECK(scene.m_Materials.empty());
        CHECK(scene.m_Textures.empty());
        CHECK(scene.m_Cameras.empty());
        CHECK(scene.m_Lights.empty());
        CHECK(scene.m_Animations.empty());
        CHECK(scene.m_InstanceData.empty());
    }

    // ------------------------------------------------------------------
    // TC-SHUT-02: Scene::Shutdown() releases all GPU buffer handles
    // ------------------------------------------------------------------
    TEST_CASE("TC-SHUT-02 Shutdown - Shutdown() releases all GPU buffer handles")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");

        {
            SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
            REQUIRE(scope.loaded);
        }

        const Scene& scene = g_Renderer.m_Scene;
        CHECK(scene.m_VertexBufferQuantized == nullptr);
        CHECK(scene.m_IndexBuffer           == nullptr);
        CHECK(scene.m_MaterialConstantsBuffer == nullptr);
        CHECK(scene.m_InstanceDataBuffer    == nullptr);
        CHECK(scene.m_MeshDataBuffer        == nullptr);
        CHECK(scene.m_LightBuffer           == nullptr);
        CHECK(scene.m_TLAS                  == nullptr);
        CHECK(scene.m_RTInstanceDescBuffer  == nullptr);
        CHECK(scene.m_BLASAddressBuffer     == nullptr);
    }

}

// ============================================================================
// TEST SUITE: Scene_GeometryBufferInit
// ============================================================================
TEST_SUITE("Scene_GeometryBufferInit")
{
        // ------------------------------------------------------------------
        // TC-SINIT-01: InitializeDefaultCube creates required geometry buffers
        // ------------------------------------------------------------------
        TEST_CASE("TC-SINIT-01 GeometryInit - InitializeDefaultCube creates GPU geometry buffers")
        {
                REQUIRE(DEV() != nullptr);
                DEV()->waitForIdle();
                Scene& scene = g_Renderer.m_Scene;
                scene.Shutdown();

                scene.InitializeDefaultCube(0, 0);
                g_Renderer.ExecutePendingCommandLists();

                CHECK(scene.m_VertexBufferQuantized != nullptr);
                CHECK(scene.m_IndexBuffer != nullptr);
                REQUIRE(!scene.m_Meshes.empty());
                REQUIRE(!scene.m_Meshes[0].m_Primitives.empty());
                CHECK(!scene.m_MeshData.empty());

                DEV()->waitForIdle();
                scene.Shutdown();
        }

        // ------------------------------------------------------------------
        // TC-SINIT-02: Sync in-memory upload appends into initialized buffers
        // ------------------------------------------------------------------
        TEST_CASE("TC-SINIT-02 GeometryInit - UploadGeometryBuffers appends after InitializeDefaultCube")
        {
                REQUIRE(DEV() != nullptr);
                DEV()->waitForIdle();
                Scene& scene = g_Renderer.m_Scene;
                scene.Shutdown();

                scene.InitializeDefaultCube(0, 0);
                g_Renderer.ExecutePendingCommandLists();

                static constexpr const char kMinimalGltf[] = R"({
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

                std::vector<srrhi::VertexQuantized> vertices;
                std::vector<uint32_t> indices;
                const bool ok = SceneLoader::LoadGLTFSceneFromMemory(
                        scene,
                        kMinimalGltf, sizeof(kMinimalGltf) - 1,
                        {},
                        vertices, indices);
                REQUIRE(ok);
                REQUIRE(!vertices.empty());

                const uint32_t preUsedVerts = scene.m_VertexBufferUsed;
                const uint32_t preUsedIdx = scene.m_IndexBufferUsed;

                scene.UploadGeometryBuffers(vertices, indices);

                CHECK(scene.m_VertexBufferQuantized != nullptr);
                CHECK(scene.m_IndexBuffer != nullptr);
                CHECK(scene.m_VertexBufferUsed == preUsedVerts + (uint32_t)vertices.size());
                CHECK(scene.m_IndexBufferUsed == preUsedIdx + (uint32_t)indices.size());

                DEV()->waitForIdle();
                scene.Shutdown();
        }
}
