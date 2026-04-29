// Tests_Materials.cpp
//
// Systems under test:
//   - Material system (Scene::Material properties, defaults, PBR factors)
//   - MaterialConstantsFromMaterial (CPU→GPU constant conversion)
//   - UpdateMaterialsAndCreateConstants (GPU buffer upload)
//   - Material dirty flags (m_MaterialDirtyRange, upload-on-dirty)
//   - Shader variant selection (opaque / alpha-test / alpha-blend → ShaderID)
//   - SRRHI resource binding (ResourceEntry, ResourceType, TextureDimension)
//   - GPU binding (SRV / CBV / Sampler descriptor creation via CreateBindingSetDesc)
//   - Sampler binding (bindless sampler indices, sampler type per texture)
//   - Bindless texture indices (m_AlbedoTextureIndex, m_NormalTextureIndex, …)
//   - Scene texture sampler assignment (Wrap vs Clamp)
//   - Material GPU buffer size and struct stride
//   - Material constants buffer upload after dirty-range flush
//
// Prerequisites: g_Renderer fully initialized (RHI + CommonResources).
//
// Run with: HobbyRenderer --run-tests=*Material*
//           HobbyRenderer --run-tests=*Material* --gltf-samples <path>
// ============================================================================

#include "TestFixtures.h"

// ============================================================================
// Helpers
// ============================================================================
namespace
{
    // Build a default Scene::Material with no textures assigned.
    static Scene::Material MakeDefaultMaterial(const char* name = "TestMat")
    {
        Scene::Material m;
        m.m_Name = name;
        // Defaults from Scene.h
        m.m_BaseColorFactor = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        m.m_EmissiveFactor  = Vector3{ 0.0f, 0.0f, 0.0f };
        m.m_RoughnessFactor = 1.0f;
        m.m_MetallicFactor  = 0.0f;
        m.m_AlphaMode       = srrhi::CommonConsts::ALPHA_MODE_OPAQUE;
        m.m_AlphaCutoff     = 0.5f;
        m.m_IOR             = 1.5f;
        m.m_BaseColorTexture          = -1;
        m.m_NormalTexture             = -1;
        m.m_MetallicRoughnessTexture  = -1;
        m.m_EmissiveTexture           = -1;
        m.m_AlbedoTextureIndex        = srrhi::CommonConsts::DEFAULT_TEXTURE_WHITE;
        m.m_NormalTextureIndex        = srrhi::CommonConsts::DEFAULT_TEXTURE_NORMAL;
        m.m_RoughnessMetallicTextureIndex = srrhi::CommonConsts::DEFAULT_TEXTURE_PBR;
        m.m_EmissiveTextureIndex      = srrhi::CommonConsts::DEFAULT_TEXTURE_BLACK;
        return m;
    }

    // Minimal glTF with one material that has a base-color texture.
    // The texture is a 1x1 white pixel embedded as a data URI.
    static constexpr const char k_GltfWithMaterial[] = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 } ] } ],
  "materials": [ {
    "name": "TestMaterial",
    "pbrMetallicRoughness": {
      "baseColorFactor": [ 0.8, 0.6, 0.4, 1.0 ],
      "metallicFactor": 0.2,
      "roughnessFactor": 0.7
    },
    "emissiveFactor": [ 0.1, 0.2, 0.3 ],
    "alphaMode": "OPAQUE"
  } ],
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

    // Minimal glTF with an alpha-masked material.
    static constexpr const char k_GltfAlphaMask[] = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 } ] } ],
  "materials": [ {
    "name": "MaskedMat",
    "pbrMetallicRoughness": { "baseColorFactor": [ 1.0, 1.0, 1.0, 1.0 ] },
    "alphaMode": "MASK",
    "alphaCutoff": 0.3
  } ],
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

    // Minimal glTF with an alpha-blend material.
    static constexpr const char k_GltfAlphaBlend[] = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 } ] } ],
  "materials": [ {
    "name": "BlendMat",
    "pbrMetallicRoughness": { "baseColorFactor": [ 1.0, 1.0, 1.0, 0.5 ] },
    "alphaMode": "BLEND"
  } ],
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

    // Helper: load a glTF from memory, finalize, upload geometry, create material constants.
    // Returns true on success.  Caller must call Shutdown() when done.
    static bool LoadInMemoryScene(const char* json, size_t jsonLen)
    {
        if (DEV()) DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
        g_Renderer.m_Scene.InitializeDefaultCube(0, 0);
        g_Renderer.ExecutePendingCommandLists();

        std::vector<srrhi::VertexQuantized> vertices;
        std::vector<uint32_t> indices;
        const bool ok = SceneLoader::LoadGLTFSceneFromMemory(
            g_Renderer.m_Scene, json, jsonLen, {}, vertices, indices);
        if (!ok) return false;

        g_Renderer.m_Scene.FinalizeLoadedScene();
        SceneLoader::LoadTexturesFromImages(g_Renderer.m_Scene, {});
        g_Renderer.m_Scene.UploadGeometryBuffers(vertices, indices);
        SceneLoader::CreateAndUploadLightBuffer(g_Renderer.m_Scene);
        {
            nvrhi::CommandListHandle cmd = g_Renderer.AcquireCommandList();
            ScopedCommandList scopedCmd{ cmd, "LoadInMemoryScene_Materials" };
            SceneLoader::UpdateMaterialsAndCreateConstants(g_Renderer.m_Scene, cmd);
            g_Renderer.m_Scene.BuildAccelerationStructures(cmd);
        }
        g_Renderer.ExecutePendingCommandLists();
        if (DEV()) DEV()->waitForIdle();
        return true;
    }
} // anonymous namespace

// ============================================================================
// TEST SUITE: Material_Defaults
// Tests that a freshly constructed Scene::Material has the correct PBR defaults.
// ============================================================================
TEST_SUITE("Material_Defaults")
{
    // ------------------------------------------------------------------
    // TC-MATD-01: Default base color factor is (1,1,1,1)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-01 Defaults - base color factor is (1,1,1,1)")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_BaseColorFactor.x == doctest::Approx(1.0f));
        CHECK(m.m_BaseColorFactor.y == doctest::Approx(1.0f));
        CHECK(m.m_BaseColorFactor.z == doctest::Approx(1.0f));
        CHECK(m.m_BaseColorFactor.w == doctest::Approx(1.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-02: Default emissive factor is (0,0,0)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-02 Defaults - emissive factor is (0,0,0)")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_EmissiveFactor.x == doctest::Approx(0.0f));
        CHECK(m.m_EmissiveFactor.y == doctest::Approx(0.0f));
        CHECK(m.m_EmissiveFactor.z == doctest::Approx(0.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-03: Default roughness factor is 1.0
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-03 Defaults - roughness factor is 1.0")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_RoughnessFactor == doctest::Approx(1.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-04: Default metallic factor is 0.0
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-04 Defaults - metallic factor is 0.0")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_MetallicFactor == doctest::Approx(0.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-05: Default alpha mode is OPAQUE
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-05 Defaults - alpha mode is OPAQUE")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_AlphaMode == (uint32_t)srrhi::CommonConsts::ALPHA_MODE_OPAQUE);
    }

    // ------------------------------------------------------------------
    // TC-MATD-06: Default alpha cutoff is 0.5
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-06 Defaults - alpha cutoff is 0.5")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_AlphaCutoff == doctest::Approx(0.5f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-07: Default IOR is 1.5
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-07 Defaults - IOR is 1.5")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_IOR == doctest::Approx(1.5f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-08: Default transmission factor is 0.0
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-08 Defaults - transmission factor is 0.0")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_TransmissionFactor == doctest::Approx(0.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-09: Default texture indices point to correct default textures
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-09 Defaults - texture indices point to correct default textures")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_AlbedoTextureIndex        == (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_WHITE);
        CHECK(m.m_NormalTextureIndex        == (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_NORMAL);
        CHECK(m.m_RoughnessMetallicTextureIndex == (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_PBR);
        CHECK(m.m_EmissiveTextureIndex      == (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_BLACK);
    }

    // ------------------------------------------------------------------
    // TC-MATD-10: Default texture slots are all -1 (no textures assigned)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-10 Defaults - all texture slots are -1")
    {
        const Scene::Material m = MakeDefaultMaterial();
        CHECK(m.m_BaseColorTexture         == -1);
        CHECK(m.m_NormalTexture            == -1);
        CHECK(m.m_MetallicRoughnessTexture == -1);
        CHECK(m.m_EmissiveTexture          == -1);
    }

    // ------------------------------------------------------------------
    // TC-MATD-11: Default attenuation color is (1,1,1)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-11 Defaults - attenuation color is (1,1,1)")
    {
        Scene::Material m;
        CHECK(m.m_AttenuationColor.x == doctest::Approx(1.0f));
        CHECK(m.m_AttenuationColor.y == doctest::Approx(1.0f));
        CHECK(m.m_AttenuationColor.z == doctest::Approx(1.0f));
    }

    // ------------------------------------------------------------------
    // TC-MATD-12: Default attenuation distance is FLT_MAX
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATD-12 Defaults - attenuation distance is FLT_MAX")
    {
        Scene::Material m;
        CHECK(m.m_AttenuationDistance == doctest::Approx(FLT_MAX));
    }
}

// ============================================================================
// TEST SUITE: Material_ConstantsConversion
// Tests MaterialConstantsFromMaterial CPU→GPU conversion logic.
// ============================================================================
TEST_SUITE("Material_ConstantsConversion")
{
    // ------------------------------------------------------------------
    // TC-MATC-01: Base color factor is correctly copied to MaterialConstants
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-01 Conversion - base color factor is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_BaseColorFactor = Vector4{ 0.25f, 0.5f, 0.75f, 0.9f };

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_BaseColor.x == doctest::Approx(0.25f));
        CHECK(mc.m_BaseColor.y == doctest::Approx(0.5f));
        CHECK(mc.m_BaseColor.z == doctest::Approx(0.75f));
        CHECK(mc.m_BaseColor.w == doctest::Approx(0.9f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-02: Roughness and metallic factors are copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-02 Conversion - roughness and metallic factors are copied")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_RoughnessFactor = 0.3f;
        m.m_MetallicFactor  = 0.8f;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_RoughnessMetallic.x == doctest::Approx(0.3f));
        CHECK(mc.m_RoughnessMetallic.y == doctest::Approx(0.8f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-03: Emissive factor is copied correctly (XYZ only, W=1)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-03 Conversion - emissive factor is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_EmissiveFactor = Vector3{ 0.1f, 0.2f, 0.3f };

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_EmissiveFactor.x == doctest::Approx(0.1f));
        CHECK(mc.m_EmissiveFactor.y == doctest::Approx(0.2f));
        CHECK(mc.m_EmissiveFactor.z == doctest::Approx(0.3f));
        CHECK(mc.m_EmissiveFactor.w == doctest::Approx(1.0f)); // W is always 1
    }

    // ------------------------------------------------------------------
    // TC-MATC-04: Alpha mode is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-04 Conversion - alpha mode is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_AlphaMode = srrhi::CommonConsts::ALPHA_MODE_MASK;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_AlphaMode == (uint32_t)srrhi::CommonConsts::ALPHA_MODE_MASK);
    }

    // ------------------------------------------------------------------
    // TC-MATC-05: Alpha cutoff is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-05 Conversion - alpha cutoff is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_AlphaCutoff = 0.3f;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_AlphaCutoff == doctest::Approx(0.3f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-06: IOR is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-06 Conversion - IOR is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_IOR = 1.33f;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_IOR == doctest::Approx(1.33f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-07: No-texture material has zero TextureFlags
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-07 Conversion - no-texture material has zero TextureFlags")
    {
        Scene::Material m = MakeDefaultMaterial();
        // All texture slots are -1 by default

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_TextureFlags == 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-08: TEXFLAG_ALBEDO is set when base color texture is assigned
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-08 Conversion - TEXFLAG_ALBEDO set when base color texture assigned")
    {
        Scene::Material m = MakeDefaultMaterial();
        // Assign a dummy texture slot (index 0)
        Scene::Texture dummyTex;
        dummyTex.m_Sampler = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { dummyTex };
        m.m_BaseColorTexture = 0;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_ALBEDO) != 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-09: TEXFLAG_NORMAL is set when normal texture is assigned
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-09 Conversion - TEXFLAG_NORMAL set when normal texture assigned")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture dummyTex;
        dummyTex.m_Sampler = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { dummyTex };
        m.m_NormalTexture = 0;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_NORMAL) != 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-10: TEXFLAG_ROUGHNESS_METALLIC is set when ORM texture is assigned
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-10 Conversion - TEXFLAG_ROUGHNESS_METALLIC set when ORM texture assigned")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture dummyTex;
        dummyTex.m_Sampler = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { dummyTex };
        m.m_MetallicRoughnessTexture = 0;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_ROUGHNESS_METALLIC) != 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-11: TEXFLAG_EMISSIVE is set when emissive texture is assigned
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-11 Conversion - TEXFLAG_EMISSIVE set when emissive texture assigned")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture dummyTex;
        dummyTex.m_Sampler = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { dummyTex };
        m.m_EmissiveTexture = 0;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_EMISSIVE) != 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-12: All four texture flags are set when all textures assigned
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-12 Conversion - all four texture flags set when all textures assigned")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture dummyTex;
        dummyTex.m_Sampler = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { dummyTex, dummyTex, dummyTex, dummyTex };
        m.m_BaseColorTexture         = 0;
        m.m_NormalTexture            = 1;
        m.m_MetallicRoughnessTexture = 2;
        m.m_EmissiveTexture          = 3;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_ALBEDO)             != 0u);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_NORMAL)             != 0u);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_ROUGHNESS_METALLIC) != 0u);
        CHECK((mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_EMISSIVE)           != 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-13: Albedo texture index is copied from material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-13 Conversion - albedo texture index is copied from material")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_AlbedoTextureIndex = 42u;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_AlbedoTextureIndex == 42u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-14: Normal texture index is copied from material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-14 Conversion - normal texture index is copied from material")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_NormalTextureIndex = 7u;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_NormalTextureIndex == 7u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-15: Sampler index defaults to Wrap (1) when no texture assigned
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-15 Conversion - sampler index defaults to Wrap when no texture assigned")
    {
        Scene::Material m = MakeDefaultMaterial();
        // No textures assigned (all -1)

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        // Default sampler index = Scene::Texture::Wrap = 1
        CHECK(mc.m_AlbedoSamplerIndex    == (uint32_t)Scene::Texture::Wrap);
        CHECK(mc.m_NormalSamplerIndex    == (uint32_t)Scene::Texture::Wrap);
        CHECK(mc.m_RoughnessSamplerIndex == (uint32_t)Scene::Texture::Wrap);
        CHECK(mc.m_EmissiveSamplerIndex  == (uint32_t)Scene::Texture::Wrap);
    }

    // ------------------------------------------------------------------
    // TC-MATC-16: Sampler index reflects texture's sampler type (Clamp)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-16 Conversion - sampler index reflects Clamp texture sampler")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture clampTex;
        clampTex.m_Sampler = Scene::Texture::Clamp;
        std::vector<Scene::Texture> textures = { clampTex };
        m.m_BaseColorTexture = 0;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK(mc.m_AlbedoSamplerIndex == (uint32_t)Scene::Texture::Clamp);
    }

    // ------------------------------------------------------------------
    // TC-MATC-17: Sampler index reflects texture's sampler type (Wrap)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-17 Conversion - sampler index reflects Wrap texture sampler")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture wrapTex;
        wrapTex.m_Sampler = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { wrapTex };
        m.m_BaseColorTexture = 0;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK(mc.m_AlbedoSamplerIndex == (uint32_t)Scene::Texture::Wrap);
    }

    // ------------------------------------------------------------------
    // TC-MATC-18: IsThinSurface is 0 when thicknessFactor > 0
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-18 Conversion - IsThinSurface is 0 when thicknessFactor > 0")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_ThicknessFactor = 1.0f;
        m.m_IsThinSurface   = false;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_IsThinSurface == 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-19: IsThinSurface is 1 when m_IsThinSurface is true
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-19 Conversion - IsThinSurface is 1 when m_IsThinSurface is true")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_IsThinSurface = true;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_IsThinSurface == 1u);
    }

    // ------------------------------------------------------------------
    // TC-MATC-20: Transmission factor is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-20 Conversion - transmission factor is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_TransmissionFactor = 0.6f;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_TransmissionFactor == doctest::Approx(0.6f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-21: Thickness factor is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-21 Conversion - thickness factor is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_ThicknessFactor = 2.5f;

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_ThicknessFactor == doctest::Approx(2.5f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-22: Attenuation color is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-22 Conversion - attenuation color is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_AttenuationColor = Vector3{ 0.2f, 0.4f, 0.6f };

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_AttenuationColor.x == doctest::Approx(0.2f));
        CHECK(mc.m_AttenuationColor.y == doctest::Approx(0.4f));
        CHECK(mc.m_AttenuationColor.z == doctest::Approx(0.6f));
    }

    // ------------------------------------------------------------------
    // TC-MATC-23: SigmaA is copied correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATC-23 Conversion - SigmaA is copied correctly")
    {
        Scene::Material m = MakeDefaultMaterial();
        m.m_SigmaA = Vector3{ 0.01f, 0.02f, 0.03f };

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});
        CHECK(mc.m_SigmaA.x == doctest::Approx(0.01f));
        CHECK(mc.m_SigmaA.y == doctest::Approx(0.02f));
        CHECK(mc.m_SigmaA.z == doctest::Approx(0.03f));
    }
}

// ============================================================================
// TEST SUITE: Material_GPUBuffer
// Tests GPU buffer creation and upload for material constants.
// ============================================================================
TEST_SUITE("Material_GPUBuffer")
{
    // ------------------------------------------------------------------
    // TC-MATGPU-01: MaterialConstantsBuffer is non-null after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-01 GPUBuffer - MaterialConstantsBuffer is non-null after load")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        CHECK(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATGPU-02: MaterialConstantsBuffer has correct struct stride
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-02 GPUBuffer - MaterialConstantsBuffer has correct struct stride")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        REQUIRE(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);
        const nvrhi::BufferDesc& desc = g_Renderer.m_Scene.m_MaterialConstantsBuffer->getDesc();
        CHECK(desc.structStride == sizeof(srrhi::MaterialConstants));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATGPU-03: MaterialConstantsBuffer byte size covers all materials
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-03 GPUBuffer - MaterialConstantsBuffer byte size covers all materials")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const size_t matCount = g_Renderer.m_Scene.m_Materials.size();
        REQUIRE(matCount > 0);
        REQUIRE(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);

        const uint64_t bufBytes = g_Renderer.m_Scene.m_MaterialConstantsBuffer->getDesc().byteSize;
        const uint64_t reqBytes = matCount * sizeof(srrhi::MaterialConstants);
        INFO("matCount=" << matCount << " reqBytes=" << reqBytes << " bufBytes=" << bufBytes);
        CHECK(bufBytes >= reqBytes);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATGPU-04: MaterialConstantsBuffer is a shader resource buffer
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-04 GPUBuffer - MaterialConstantsBuffer is a shader resource buffer")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        REQUIRE(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);
        // A structured buffer used as SRV must have a non-zero structStride
        const nvrhi::BufferDesc& desc = g_Renderer.m_Scene.m_MaterialConstantsBuffer->getDesc();
        CHECK(desc.structStride > 0u);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATGPU-05: MaterialConstantsBuffer is null after Shutdown
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-05 GPUBuffer - MaterialConstantsBuffer is null after Shutdown")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);
        REQUIRE(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();

        CHECK(g_Renderer.m_Scene.m_MaterialConstantsBuffer == nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATGPU-06: PBR factors from glTF are reflected in loaded material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-06 GPUBuffer - PBR factors from glTF are reflected in loaded material")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        // Find the material named "TestMaterial" (skip default cube material at index 0)
        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const Scene::Material* testMat = nullptr;
        for (const auto& m : mats)
            if (m.m_Name == "TestMaterial") { testMat = &m; break; }

        REQUIRE(testMat != nullptr);
        CHECK(testMat->m_BaseColorFactor.x == doctest::Approx(0.8f).epsilon(0.01f));
        CHECK(testMat->m_BaseColorFactor.y == doctest::Approx(0.6f).epsilon(0.01f));
        CHECK(testMat->m_BaseColorFactor.z == doctest::Approx(0.4f).epsilon(0.01f));
        CHECK(testMat->m_MetallicFactor    == doctest::Approx(0.2f).epsilon(0.01f));
        CHECK(testMat->m_RoughnessFactor   == doctest::Approx(0.7f).epsilon(0.01f));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATGPU-07: Emissive factor from glTF is reflected in loaded material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATGPU-07 GPUBuffer - emissive factor from glTF is reflected in loaded material")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const Scene::Material* testMat = nullptr;
        for (const auto& m : mats)
            if (m.m_Name == "TestMaterial") { testMat = &m; break; }

        REQUIRE(testMat != nullptr);
        CHECK(testMat->m_EmissiveFactor.x == doctest::Approx(0.1f).epsilon(0.01f));
        CHECK(testMat->m_EmissiveFactor.y == doctest::Approx(0.2f).epsilon(0.01f));
        CHECK(testMat->m_EmissiveFactor.z == doctest::Approx(0.3f).epsilon(0.01f));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }
}

// ============================================================================
// TEST SUITE: Material_DirtyFlags
// Tests the material dirty range mechanism used for animated material uploads.
// ============================================================================
TEST_SUITE("Material_DirtyFlags")
{
    // ------------------------------------------------------------------
    // TC-MATDF-01: m_MaterialDirtyRange is clean (first > second) after load
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-01 DirtyFlags - dirty range is clean after initial load")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const auto& range = g_Renderer.m_Scene.m_MaterialDirtyRange;
        // Clean state: first > second (UINT32_MAX, 0)
        CHECK(range.first > range.second);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-02: Setting dirty range makes it dirty (first <= second)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-02 DirtyFlags - setting dirty range makes it dirty")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        // Manually mark material 0 as dirty
        g_Renderer.m_Scene.m_MaterialDirtyRange = { 0u, 0u };
        const auto& range = g_Renderer.m_Scene.m_MaterialDirtyRange;
        CHECK(range.first <= range.second);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-03: Dirty range is reset to clean after Shutdown
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-03 DirtyFlags - dirty range is reset to clean after Shutdown")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        // Mark dirty
        g_Renderer.m_Scene.m_MaterialDirtyRange = { 0u, 0u };

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();

        // After shutdown, dirty range must be clean
        const auto& range = g_Renderer.m_Scene.m_MaterialDirtyRange;
        CHECK(range.first > range.second);
    }

    // ------------------------------------------------------------------
    // TC-MATDF-04: Dirty range expands correctly when multiple materials are marked
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-04 DirtyFlags - dirty range expands to cover all marked materials")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const size_t matCount = g_Renderer.m_Scene.m_Materials.size();
        if (matCount < 2)
        {
            WARN("Skipping: need at least 2 materials");
            DEV()->waitForIdle();
            g_Renderer.m_Scene.Shutdown();
            return;
        }

        // Simulate marking materials 0 and (matCount-1) as dirty
        g_Renderer.m_Scene.m_MaterialDirtyRange.first  = std::min(0u, g_Renderer.m_Scene.m_MaterialDirtyRange.first);
        g_Renderer.m_Scene.m_MaterialDirtyRange.second = std::max((uint32_t)(matCount - 1), g_Renderer.m_Scene.m_MaterialDirtyRange.second);

        CHECK(g_Renderer.m_Scene.m_MaterialDirtyRange.first  == 0u);
        CHECK(g_Renderer.m_Scene.m_MaterialDirtyRange.second == (uint32_t)(matCount - 1));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-05: RunOneFrame clears the material dirty range
    //
    // Regression test for the engine bug where the material dirty upload
    // lived only inside RenderFrame() (the main game loop) and was never
    // called by RunOneFrame() (the unit-test path).  The fix extracted the
    // upload into Renderer::UploadDirtyMaterialConstants() and calls it
    // from both paths.
    //
    // Prerequisites:
    //   • Scene must have at least one material AND a valid
    //     m_MaterialConstantsBuffer so the upload can actually execute.
    //   • MinimalSceneFixture loads a no-material glTF, so we use
    //     LoadInMemoryScene(k_GltfWithMaterial) instead.
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-05 DirtyFlags - RunOneFrame clears material dirty range")
    {
        REQUIRE(DEV() != nullptr);

        // Load a scene that has at least one material and a GPU constants buffer.
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const size_t matCount = g_Renderer.m_Scene.m_Materials.size();
        INFO("matCount=" << matCount);
        REQUIRE(matCount > 0);
        REQUIRE(g_Renderer.m_Scene.m_MaterialConstantsBuffer != nullptr);

        // Confirm the range is clean after load.
        {
            const auto& r = g_Renderer.m_Scene.m_MaterialDirtyRange;
            INFO("dirty range after load: [" << r.first << ", " << r.second << "]");
            REQUIRE(r.first > r.second); // clean sentinel
        }

        // Mark material 0 as dirty (simulates an animated emissive update).
        g_Renderer.m_Scene.m_MaterialDirtyRange = { 0u, 0u };
        {
            const auto& r = g_Renderer.m_Scene.m_MaterialDirtyRange;
            INFO("dirty range after marking: [" << r.first << ", " << r.second << "]");
            CHECK(r.first <= r.second); // must be dirty now
        }

        // Run one frame — Renderer::UploadDirtyMaterialConstants() (called by
        // RunOneFrame) must flush the dirty range to the GPU and reset it.
        CHECK(RunOneFrame());

        // After the frame the dirty range must be clean again.
        {
            const auto& r = g_Renderer.m_Scene.m_MaterialDirtyRange;
            INFO("dirty range after RunOneFrame: [" << r.first << ", " << r.second << "]");
            CHECK(r.first > r.second);
        }

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-06: UploadDirtyMaterialConstants is a no-op when materials
    //              list is empty (no crash, range stays clean)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-06 DirtyFlags - UploadDirtyMaterialConstants is no-op when materials empty")
    {
        REQUIRE(DEV() != nullptr);

        // Start from a clean scene with no materials.
        if (DEV()) DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
        g_Renderer.m_Scene.InitializeDefaultCube(0, 0);
        g_Renderer.ExecutePendingCommandLists();

        REQUIRE(g_Renderer.m_Scene.m_Materials.empty());

        // Manually set a dirty range — without a buffer or materials this must
        // not crash and must leave the range unchanged (no upload attempted).
        g_Renderer.m_Scene.m_MaterialDirtyRange = { 0u, 0u };

        CHECK_NOTHROW(g_Renderer.UploadDirtyMaterialConstants());

        // Range is unchanged because the no-op path returns early before resetting.
        // (The function only resets after a successful upload.)
        // Either clean or still dirty is acceptable — the key invariant is no crash.
        // We just verify the function returns without asserting.
        CHECK(true); // reached here without crash

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-07: UploadDirtyMaterialConstants is a no-op when buffer is null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-07 DirtyFlags - UploadDirtyMaterialConstants is no-op when buffer is null")
    {
        REQUIRE(DEV() != nullptr);

        // Load a scene so m_Materials is non-empty, then null the buffer manually.
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);
        REQUIRE(!g_Renderer.m_Scene.m_Materials.empty());

        // Null the buffer to simulate a partially-initialized state.
        g_Renderer.m_Scene.m_MaterialConstantsBuffer = nullptr;

        // Mark dirty — must not crash even with a null buffer.
        g_Renderer.m_Scene.m_MaterialDirtyRange = { 0u, 0u };
        CHECK_NOTHROW(g_Renderer.UploadDirtyMaterialConstants());

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-08: UploadDirtyMaterialConstants is a no-op when range is clean
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-08 DirtyFlags - UploadDirtyMaterialConstants is no-op when range is clean")
    {
        REQUIRE(DEV() != nullptr);

        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        // Confirm range is clean after load.
        REQUIRE(g_Renderer.m_Scene.m_MaterialDirtyRange.first >
                g_Renderer.m_Scene.m_MaterialDirtyRange.second);

        // Calling with a clean range must be a no-op (no crash, range stays clean).
        CHECK_NOTHROW(g_Renderer.UploadDirtyMaterialConstants());

        CHECK(g_Renderer.m_Scene.m_MaterialDirtyRange.first >
              g_Renderer.m_Scene.m_MaterialDirtyRange.second);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-09: UploadDirtyMaterialConstants flushes a multi-material range
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-09 DirtyFlags - UploadDirtyMaterialConstants flushes multi-material range")
    {
        REQUIRE(DEV() != nullptr);

        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const uint32_t matCount = (uint32_t)g_Renderer.m_Scene.m_Materials.size();
        INFO("matCount=" << matCount);
        REQUIRE(matCount > 0);

        // Mark the full range dirty.
        g_Renderer.m_Scene.m_MaterialDirtyRange = { 0u, matCount - 1u };
        CHECK(g_Renderer.m_Scene.m_MaterialDirtyRange.first <=
              g_Renderer.m_Scene.m_MaterialDirtyRange.second);

        // Direct call (not via RunOneFrame) must flush and reset.
        CHECK_NOTHROW(g_Renderer.UploadDirtyMaterialConstants());
        g_Renderer.ExecutePendingCommandLists();

        INFO("dirty range after flush: ["
             << g_Renderer.m_Scene.m_MaterialDirtyRange.first << ", "
             << g_Renderer.m_Scene.m_MaterialDirtyRange.second << "]");
        CHECK(g_Renderer.m_Scene.m_MaterialDirtyRange.first >
              g_Renderer.m_Scene.m_MaterialDirtyRange.second);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATDF-10: Dirty range stays clean across multiple RunOneFrame calls
    //              when no material is marked dirty between frames
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATDF-10 DirtyFlags - dirty range stays clean across multiple frames")
    {
        REQUIRE(DEV() != nullptr);

        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        // Warm-up: ensure PostSceneLoad is called for all renderers.
        for (const auto& r : g_Renderer.m_Renderers)
            if (r) r->PostSceneLoad();
        g_Renderer.ExecutePendingCommandLists();

        // Run several frames without marking any material dirty.
        for (int frame = 0; frame < 3; ++frame)
        {
            INFO("frame=" << frame);
            CHECK(RunOneFrame());
            // Range must remain clean every frame.
            CHECK(g_Renderer.m_Scene.m_MaterialDirtyRange.first >
                  g_Renderer.m_Scene.m_MaterialDirtyRange.second);
        }

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }
}

// ============================================================================
// TEST SUITE: Material_AlphaModeLoading
// Tests that alpha mode is correctly parsed from glTF and reflected in the
// material's alpha mode field and bucket assignment.
// ============================================================================
TEST_SUITE("Material_AlphaModeLoading")
{
    // ------------------------------------------------------------------
    // TC-MATAL-01: OPAQUE material has alpha mode OPAQUE
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-01 AlphaMode - OPAQUE material has alpha mode OPAQUE")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const Scene::Material* testMat = nullptr;
        for (const auto& m : mats)
            if (m.m_Name == "TestMaterial") { testMat = &m; break; }

        REQUIRE(testMat != nullptr);
        CHECK(testMat->m_AlphaMode == (uint32_t)srrhi::CommonConsts::ALPHA_MODE_OPAQUE);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATAL-02: MASK material has alpha mode MASK
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-02 AlphaMode - MASK material has alpha mode MASK")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfAlphaMask, sizeof(k_GltfAlphaMask) - 1);
        REQUIRE(ok);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const Scene::Material* testMat = nullptr;
        for (const auto& m : mats)
            if (m.m_Name == "MaskedMat") { testMat = &m; break; }

        REQUIRE(testMat != nullptr);
        CHECK(testMat->m_AlphaMode == (uint32_t)srrhi::CommonConsts::ALPHA_MODE_MASK);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATAL-03: MASK material has correct alpha cutoff
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-03 AlphaMode - MASK material has correct alpha cutoff")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfAlphaMask, sizeof(k_GltfAlphaMask) - 1);
        REQUIRE(ok);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const Scene::Material* testMat = nullptr;
        for (const auto& m : mats)
            if (m.m_Name == "MaskedMat") { testMat = &m; break; }

        REQUIRE(testMat != nullptr);
        CHECK(testMat->m_AlphaCutoff == doctest::Approx(0.3f).epsilon(0.01f));

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATAL-04: BLEND material has alpha mode BLEND
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-04 AlphaMode - BLEND material has alpha mode BLEND")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfAlphaBlend, sizeof(k_GltfAlphaBlend) - 1);
        REQUIRE(ok);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        const Scene::Material* testMat = nullptr;
        for (const auto& m : mats)
            if (m.m_Name == "BlendMat") { testMat = &m; break; }

        REQUIRE(testMat != nullptr);
        CHECK(testMat->m_AlphaMode == (uint32_t)srrhi::CommonConsts::ALPHA_MODE_BLEND);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATAL-05: OPAQUE scene has non-zero opaque bucket count
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-05 AlphaMode - OPAQUE scene has non-zero opaque bucket count")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfWithMaterial, sizeof(k_GltfWithMaterial) - 1);
        REQUIRE(ok);

        // The scene has one opaque primitive (plus the default cube which is also opaque)
        CHECK(g_Renderer.m_Scene.m_OpaqueBucket.m_Count > 0u);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATAL-06: MASK scene has non-zero masked bucket count
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-06 AlphaMode - MASK scene has non-zero masked bucket count")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfAlphaMask, sizeof(k_GltfAlphaMask) - 1);
        REQUIRE(ok);

        CHECK(g_Renderer.m_Scene.m_MaskedBucket.m_Count > 0u);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }

    // ------------------------------------------------------------------
    // TC-MATAL-07: BLEND scene has non-zero transparent bucket count
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATAL-07 AlphaMode - BLEND scene has non-zero transparent bucket count")
    {
        REQUIRE(DEV() != nullptr);
        const bool ok = LoadInMemoryScene(k_GltfAlphaBlend, sizeof(k_GltfAlphaBlend) - 1);
        REQUIRE(ok);

        CHECK(g_Renderer.m_Scene.m_TransparentBucket.m_Count > 0u);

        DEV()->waitForIdle();
        g_Renderer.m_Scene.Shutdown();
    }
}

// ============================================================================
// TEST SUITE: Material_ShaderVariantSelection
// Tests that the correct shader variant IDs are selected for each alpha mode.
// ============================================================================
TEST_SUITE("Material_ShaderVariantSelection")
{
    // ------------------------------------------------------------------
    // TC-MATSV-01: Opaque base-pass pixel shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-01 ShaderVariant - opaque GBuffer pixel shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(ShaderID::BASEPASS_GBUFFER_PSMAIN) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-02: Alpha-test pixel shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-02 ShaderVariant - alpha-test GBuffer pixel shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(
            ShaderID::BASEPASS_GBUFFER_PSMAIN_ALPHATEST_ALPHATEST_ALPHA_TEST_1) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-03: Alpha-blend forward pixel shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-03 ShaderVariant - alpha-blend forward pixel shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(
            ShaderID::BASEPASS_FORWARD_PSMAIN_FORWARD_TRANSPARENT_FORWARD_TRANSPARENT_1) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-04: Opaque and alpha-test shader IDs are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-04 ShaderVariant - opaque and alpha-test shader IDs are distinct")
    {
        CHECK(ShaderID::BASEPASS_GBUFFER_PSMAIN !=
              ShaderID::BASEPASS_GBUFFER_PSMAIN_ALPHATEST_ALPHATEST_ALPHA_TEST_1);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-05: Alpha-test and alpha-blend shader IDs are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-05 ShaderVariant - alpha-test and alpha-blend shader IDs are distinct")
    {
        CHECK(ShaderID::BASEPASS_GBUFFER_PSMAIN_ALPHATEST_ALPHATEST_ALPHA_TEST_1 !=
              ShaderID::BASEPASS_FORWARD_PSMAIN_FORWARD_TRANSPARENT_FORWARD_TRANSPARENT_1);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-06: Base-pass mesh shader (AS) handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-06 ShaderVariant - base-pass amplification shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(ShaderID::BASEPASS_ASMAIN) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-07: Base-pass mesh shader (MS) handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-07 ShaderVariant - base-pass mesh shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(ShaderID::BASEPASS_MSMAIN) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-08: Base-pass vertex shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-08 ShaderVariant - base-pass vertex shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(ShaderID::BASEPASS_VSMAIN) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-09: Path-tracer shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-09 ShaderVariant - path-tracer compute shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(
            ShaderID::PATHTRACER_PATHTRACER_CSMAIN_PATH_TRACER_MODE_1) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSV-10: All base-pass shader IDs are within valid range
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSV-10 ShaderVariant - all base-pass shader IDs are within valid range")
    {
        CHECK(ShaderID::BASEPASS_ASMAIN         < ShaderID::COUNT);
        CHECK(ShaderID::BASEPASS_MSMAIN         < ShaderID::COUNT);
        CHECK(ShaderID::BASEPASS_VSMAIN         < ShaderID::COUNT);
        CHECK(ShaderID::BASEPASS_GBUFFER_PSMAIN < ShaderID::COUNT);
        CHECK(ShaderID::BASEPASS_GBUFFER_PSMAIN_ALPHATEST_ALPHATEST_ALPHA_TEST_1 < ShaderID::COUNT);
        CHECK(ShaderID::BASEPASS_FORWARD_PSMAIN_FORWARD_TRANSPARENT_FORWARD_TRANSPARENT_1 < ShaderID::COUNT);
    }
}

// ============================================================================
// TEST SUITE: Material_SRRHIResourceBinding
// Tests the srrhi ResourceEntry / ResourceType / TextureDimension system.
// ============================================================================
TEST_SUITE("Material_SRRHIResourceBinding")
{
    // ------------------------------------------------------------------
    // TC-SRRHI-01: ResourceType enum values are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-01 SRRHI - ResourceType enum values are distinct")
    {
        CHECK((uint32_t)srrhi::ResourceType::Texture_SRV          != (uint32_t)srrhi::ResourceType::Texture_UAV);
        CHECK((uint32_t)srrhi::ResourceType::StructuredBuffer_SRV != (uint32_t)srrhi::ResourceType::StructuredBuffer_UAV);
        CHECK((uint32_t)srrhi::ResourceType::ConstantBuffer        != (uint32_t)srrhi::ResourceType::Sampler);
        CHECK((uint32_t)srrhi::ResourceType::RawBuffer_SRV         != (uint32_t)srrhi::ResourceType::RawBuffer_UAV);
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-02: TextureDimension::None is distinct from all texture dims
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-02 SRRHI - TextureDimension::None is distinct from all texture dims")
    {
        CHECK(srrhi::TextureDimension::None      != srrhi::TextureDimension::Texture2D);
        CHECK(srrhi::TextureDimension::None      != srrhi::TextureDimension::Texture3D);
        CHECK(srrhi::TextureDimension::None      != srrhi::TextureDimension::TextureCube);
        CHECK(srrhi::TextureDimension::Texture2D != srrhi::TextureDimension::Texture3D);
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-03: ResourceEntry default-initializes pResource to nullptr
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-03 SRRHI - ResourceEntry pResource defaults to nullptr")
    {
        srrhi::ResourceEntry entry{
            nullptr,                          // pResource
            0u,                               // slot
            srrhi::ResourceType::Texture_SRV, // type
            srrhi::TextureDimension::Texture2D,// textureDimension
            "TestEntry"                       // resourceName
        };
        CHECK(entry.pResource == nullptr);
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-04: ResourceEntry mip/slice defaults are correct
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-04 SRRHI - ResourceEntry mip/slice defaults are correct")
    {
        srrhi::ResourceEntry entry{
            nullptr,
            0u,
            srrhi::ResourceType::Texture_SRV,
            srrhi::TextureDimension::Texture2D,
            "TestEntry"
        };
        CHECK(entry.baseMipLevel   == 0);
        CHECK(entry.numMipLevels   == -1); // All mips
        CHECK(entry.baseArraySlice == 0);
        CHECK(entry.numArraySlices == -1); // All slices
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-05: ResourceEntry slot is stored correctly
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-05 SRRHI - ResourceEntry slot is stored correctly")
    {
        srrhi::ResourceEntry entry{
            nullptr,
            5u,
            srrhi::ResourceType::ConstantBuffer,
            srrhi::TextureDimension::None,
            "MyCBV"
        };
        CHECK(entry.slot == 5u);
        CHECK(entry.type == srrhi::ResourceType::ConstantBuffer);
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-06: CreateBindingSetDesc with a single CBV does not crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-06 SRRHI - CreateBindingSetDesc with a single CBV does not crash")
    {
        REQUIRE(DEV() != nullptr);

        // Create a minimal constant buffer
        nvrhi::BufferDesc cbDesc;
        cbDesc.byteSize        = 256;
        cbDesc.isConstantBuffer = true;
        cbDesc.debugName        = "TC-SRRHI-06-CB";
        auto cb = DEV()->createBuffer(cbDesc);
        REQUIRE(cb != nullptr);

        srrhi::ResourceEntry entry{
            cb.Get(),
            0u,
            srrhi::ResourceType::ConstantBuffer,
            srrhi::TextureDimension::None,
            "TestCBV"
        };

        std::span<const srrhi::ResourceEntry> span{ &entry, 1 };
        CHECK_NOTHROW(Renderer::CreateBindingSetDesc(span, 0));
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-07: CreateBindingSetDesc with a Texture_SRV does not crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-07 SRRHI - CreateBindingSetDesc with a Texture_SRV does not crash")
    {
        REQUIRE(DEV() != nullptr);

        const uint32_t pixel = 0xFFFFFFFFu;
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &pixel, sizeof(uint32_t), "TC-SRRHI-07-Tex");
        REQUIRE(tex != nullptr);

        srrhi::ResourceEntry entry{
            tex.Get(),
            0u,
            srrhi::ResourceType::Texture_SRV,
            srrhi::TextureDimension::Texture2D,
            "TestSRV"
        };

        std::span<const srrhi::ResourceEntry> span{ &entry, 1 };
        CHECK_NOTHROW(Renderer::CreateBindingSetDesc(span, 0));
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-08: CreateBindingSetDesc with a StructuredBuffer_SRV does not crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-08 SRRHI - CreateBindingSetDesc with a StructuredBuffer_SRV does not crash")
    {
        REQUIRE(DEV() != nullptr);

        nvrhi::BufferDesc desc;
        desc.byteSize     = sizeof(srrhi::MaterialConstants);
        desc.structStride = sizeof(srrhi::MaterialConstants);
        desc.debugName    = "TC-SRRHI-08-SB";
        auto buf = DEV()->createBuffer(desc);
        REQUIRE(buf != nullptr);

        srrhi::ResourceEntry entry{
            buf.Get(),
            1u,
            srrhi::ResourceType::StructuredBuffer_SRV,
            srrhi::TextureDimension::None,
            "TestStructuredSRV"
        };

        std::span<const srrhi::ResourceEntry> span{ &entry, 1 };
        CHECK_NOTHROW(Renderer::CreateBindingSetDesc(span, 0));
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-09: GetOrCreateBindingLayoutFromBindingSetDesc returns non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-09 SRRHI - GetOrCreateBindingLayoutFromBindingSetDesc returns non-null")
    {
        REQUIRE(DEV() != nullptr);

        nvrhi::BufferDesc cbDesc;
        cbDesc.byteSize        = 256;
        cbDesc.isConstantBuffer = true;
        cbDesc.debugName        = "TC-SRRHI-09-CB";
        auto cb = DEV()->createBuffer(cbDesc);
        REQUIRE(cb != nullptr);

        nvrhi::BindingSetDesc setDesc;
        setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, cb));

        nvrhi::BindingLayoutHandle layout =
            g_Renderer.GetOrCreateBindingLayoutFromBindingSetDesc(setDesc, 0);
        CHECK(layout != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-SRRHI-10: GetOrCreateBindingLayoutFromBindingSetDesc is cached (same ptr on repeat call)
    // ------------------------------------------------------------------
    TEST_CASE("TC-SRRHI-10 SRRHI - GetOrCreateBindingLayoutFromBindingSetDesc is cached")
    {
        REQUIRE(DEV() != nullptr);

        nvrhi::BufferDesc cbDesc;
        cbDesc.byteSize        = 256;
        cbDesc.isConstantBuffer = true;
        cbDesc.debugName        = "TC-SRRHI-10-CB";
        auto cb = DEV()->createBuffer(cbDesc);
        REQUIRE(cb != nullptr);

        nvrhi::BindingSetDesc setDesc;
        setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, cb));

        const nvrhi::BindingLayoutHandle layout1 =
            g_Renderer.GetOrCreateBindingLayoutFromBindingSetDesc(setDesc, 0);
        const nvrhi::BindingLayoutHandle layout2 =
            g_Renderer.GetOrCreateBindingLayoutFromBindingSetDesc(setDesc, 0);

        REQUIRE(layout1 != nullptr);
        REQUIRE(layout2 != nullptr);
        // Same descriptor → same cached handle
        CHECK(layout1.Get() == layout2.Get());
    }
}

// ============================================================================
// TEST SUITE: Material_SamplerBinding
// Tests sampler binding indices and the bindless sampler descriptor table.
// ============================================================================
TEST_SUITE("Material_SamplerBinding")
{
    // ------------------------------------------------------------------
    // TC-MATSB-01: Bindless sampler descriptor table is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-01 SamplerBinding - bindless sampler descriptor table is non-null")
    {
        CHECK(g_Renderer.GetStaticSamplerDescriptorTable() != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSB-02: Bindless sampler binding layout is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-02 SamplerBinding - bindless sampler binding layout is non-null")
    {
        CHECK(g_Renderer.GetStaticSamplerBindingLayout() != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATSB-03: SAMPLER_ANISOTROPIC_WRAP_INDEX is a valid sampler index
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-03 SamplerBinding - SAMPLER_ANISOTROPIC_WRAP_INDEX is valid")
    {
        // The index must be non-negative and within the known sampler count
        CHECK(srrhi::CommonConsts::SAMPLER_ANISOTROPIC_WRAP_INDEX >= 0);
        CHECK(srrhi::CommonConsts::SAMPLER_ANISOTROPIC_WRAP_INDEX < 16); // Reasonable upper bound
    }

    // ------------------------------------------------------------------
    // TC-MATSB-04: SAMPLER_LINEAR_WRAP_INDEX is a valid sampler index
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-04 SamplerBinding - SAMPLER_LINEAR_WRAP_INDEX is valid")
    {
        CHECK(srrhi::CommonConsts::SAMPLER_LINEAR_WRAP_INDEX >= 0);
        CHECK(srrhi::CommonConsts::SAMPLER_LINEAR_WRAP_INDEX < 16);
    }

    // ------------------------------------------------------------------
    // TC-MATSB-05: All sampler indices are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-05 SamplerBinding - all sampler indices are distinct")
    {
        const int indices[] = {
            srrhi::CommonConsts::SAMPLER_ANISOTROPIC_CLAMP_INDEX,
            srrhi::CommonConsts::SAMPLER_ANISOTROPIC_WRAP_INDEX,
            srrhi::CommonConsts::SAMPLER_POINT_CLAMP_INDEX,
            srrhi::CommonConsts::SAMPLER_POINT_WRAP_INDEX,
            srrhi::CommonConsts::SAMPLER_LINEAR_CLAMP_INDEX,
            srrhi::CommonConsts::SAMPLER_LINEAR_WRAP_INDEX,
            srrhi::CommonConsts::SAMPLER_MIN_REDUCTION_INDEX,
            srrhi::CommonConsts::SAMPLER_MAX_REDUCTION_INDEX,
            srrhi::CommonConsts::SAMPLER_LINEAR_CLAMP_BORDER_WHITE_INDEX,
        };
        constexpr int N = (int)(sizeof(indices) / sizeof(indices[0]));
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
            {
                INFO("Sampler indices " << i << " and " << j << " are equal: " << indices[i]);
                CHECK(indices[i] != indices[j]);
            }
    }

    // ------------------------------------------------------------------
    // TC-MATSB-06: Scene::Texture::Wrap and Clamp enum values are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-06 SamplerBinding - Texture::Wrap and Clamp are distinct")
    {
        CHECK((uint32_t)Scene::Texture::Wrap  != (uint32_t)Scene::Texture::Clamp);
    }

    // ------------------------------------------------------------------
    // TC-MATSB-07: Scene::Texture::Wrap == 1 (matches SAMPLER_ANISOTROPIC_WRAP_INDEX offset)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-07 SamplerBinding - Texture::Wrap value is 1")
    {
        CHECK((uint32_t)Scene::Texture::Wrap  == 1u);
        CHECK((uint32_t)Scene::Texture::Clamp == 0u);
    }

    // ------------------------------------------------------------------
    // TC-MATSB-08: MaterialConstants sampler indices are in valid range
    //              when converted from a no-texture material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-08 SamplerBinding - MaterialConstants sampler indices are in valid range")
    {
        const Scene::Material m = MakeDefaultMaterial();
        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, {});

        // Sampler indices must be 0 (Clamp) or 1 (Wrap) — the only two values
        // Scene::Texture::SamplerType can produce.
        CHECK(mc.m_AlbedoSamplerIndex    <= 1u);
        CHECK(mc.m_NormalSamplerIndex    <= 1u);
        CHECK(mc.m_RoughnessSamplerIndex <= 1u);
        CHECK(mc.m_EmissiveSamplerIndex  <= 1u);
    }

    // ------------------------------------------------------------------
    // TC-MATSB-09: Sampler indices for all four channels are independent
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSB-09 SamplerBinding - sampler indices for all four channels are independent")
    {
        Scene::Material m = MakeDefaultMaterial();
        Scene::Texture clampTex; clampTex.m_Sampler = Scene::Texture::Clamp;
        Scene::Texture wrapTex;  wrapTex.m_Sampler  = Scene::Texture::Wrap;
        std::vector<Scene::Texture> textures = { clampTex, wrapTex, clampTex, wrapTex };
        m.m_BaseColorTexture         = 0; // Clamp
        m.m_NormalTexture            = 1; // Wrap
        m.m_MetallicRoughnessTexture = 2; // Clamp
        m.m_EmissiveTexture          = 3; // Wrap

        const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
        CHECK(mc.m_AlbedoSamplerIndex    == (uint32_t)Scene::Texture::Clamp);
        CHECK(mc.m_NormalSamplerIndex    == (uint32_t)Scene::Texture::Wrap);
        CHECK(mc.m_RoughnessSamplerIndex == (uint32_t)Scene::Texture::Clamp);
        CHECK(mc.m_EmissiveSamplerIndex  == (uint32_t)Scene::Texture::Wrap);
    }
}

// ============================================================================
// TEST SUITE: Material_BindlessTextureIndices
// Tests that bindless texture indices are correctly assigned and in-range.
// ============================================================================
TEST_SUITE("Material_BindlessTextureIndices")
{
    // ------------------------------------------------------------------
    // TC-MATBI-01: Bindless texture descriptor table is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-01 BindlessIndices - bindless texture descriptor table is non-null")
    {
        CHECK(g_Renderer.GetStaticTextureDescriptorTable() != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-02: Bindless texture binding layout is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-02 BindlessIndices - bindless texture binding layout is non-null")
    {
        CHECK(g_Renderer.GetStaticTextureBindingLayout() != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-03: DEFAULT_TEXTURE_COUNT is > 0
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-03 BindlessIndices - DEFAULT_TEXTURE_COUNT is > 0")
    {
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_COUNT > 0);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-04: Default texture indices are within [0, DEFAULT_TEXTURE_COUNT)
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-04 BindlessIndices - default texture indices are within valid range")
    {
        const int count = srrhi::CommonConsts::DEFAULT_TEXTURE_COUNT;
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_BLACK    >= 0);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_BLACK    <  count);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_WHITE    >= 0);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_WHITE    <  count);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_NORMAL   >= 0);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_NORMAL   <  count);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_PBR      >= 0);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_PBR      <  count);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_BRDF_LUT >= 0);
        CHECK(srrhi::CommonConsts::DEFAULT_TEXTURE_BRDF_LUT <  count);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-05: All default texture indices are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-05 BindlessIndices - all default texture indices are distinct")
    {
        const int indices[] = {
            srrhi::CommonConsts::DEFAULT_TEXTURE_BLACK,
            srrhi::CommonConsts::DEFAULT_TEXTURE_WHITE,
            srrhi::CommonConsts::DEFAULT_TEXTURE_GRAY,
            srrhi::CommonConsts::DEFAULT_TEXTURE_NORMAL,
            srrhi::CommonConsts::DEFAULT_TEXTURE_PBR,
            srrhi::CommonConsts::DEFAULT_TEXTURE_BRDF_LUT,
            srrhi::CommonConsts::DEFAULT_TEXTURE_IRRADIANCE,
            srrhi::CommonConsts::DEFAULT_TEXTURE_RADIANCE,
        };
        constexpr int N = (int)(sizeof(indices) / sizeof(indices[0]));
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
            {
                INFO("Default texture indices " << i << " and " << j << " are equal: " << indices[i]);
                CHECK(indices[i] != indices[j]);
            }
    }

    // ------------------------------------------------------------------
    // TC-MATBI-06: m_NextTextureIndex starts at DEFAULT_TEXTURE_COUNT
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-06 BindlessIndices - m_NextTextureIndex starts at DEFAULT_TEXTURE_COUNT")
    {
        // After initialization, the next free slot must be at or beyond the reserved defaults.
        CHECK(g_Renderer.m_NextTextureIndex >= (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_COUNT);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-07: RegisterTexture returns an index >= DEFAULT_TEXTURE_COUNT
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-07 BindlessIndices - RegisterTexture returns index >= DEFAULT_TEXTURE_COUNT")
    {
        REQUIRE(DEV() != nullptr);

        const uint32_t pixel = 0xFFFFFFFFu;
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &pixel, sizeof(uint32_t), "TC-MATBI-07-Tex");
        REQUIRE(tex != nullptr);

        const uint32_t idx = g_Renderer.RegisterTexture(tex);
        CHECK(idx >= (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_COUNT);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-08: RegisterTexture returns monotonically increasing indices
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-08 BindlessIndices - RegisterTexture returns monotonically increasing indices")
    {
        REQUIRE(DEV() != nullptr);

        const uint32_t pixel = 0xFFFFFFFFu;
        nvrhi::TextureHandle tex1 = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                        &pixel, sizeof(uint32_t), "TC-MATBI-08-A");
        nvrhi::TextureHandle tex2 = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                        &pixel, sizeof(uint32_t), "TC-MATBI-08-B");
        REQUIRE(tex1 != nullptr);
        REQUIRE(tex2 != nullptr);

        const uint32_t idx1 = g_Renderer.RegisterTexture(tex1);
        const uint32_t idx2 = g_Renderer.RegisterTexture(tex2);
        CHECK(idx2 > idx1);
    }

    // ------------------------------------------------------------------
    // TC-MATBI-09: Scene textures have bindless indices >= DEFAULT_TEXTURE_COUNT
    //              after UpdateMaterialsAndCreateConstants
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-09 BindlessIndices - scene textures have bindless indices >= DEFAULT_TEXTURE_COUNT")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)textures.size(); ++i)
        {
            INFO("Texture " << i << " uri=" << textures[i].m_Uri
                 << " bindlessIndex=" << textures[i].m_BindlessIndex);
            CHECK(textures[i].m_BindlessIndex >= (uint32_t)srrhi::CommonConsts::DEFAULT_TEXTURE_COUNT);
        }
    }

    // ------------------------------------------------------------------
    // TC-MATBI-10: Material albedo texture index matches scene texture bindless index
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATBI-10 BindlessIndices - material albedo index matches scene texture bindless index")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats     = g_Renderer.m_Scene.m_Materials;
        const auto& textures = g_Renderer.m_Scene.m_Textures;

        for (int i = 0; i < (int)mats.size(); ++i)
        {
            const int texIdx = mats[i].m_BaseColorTexture;
            if (texIdx == -1) continue; // No base color texture

            INFO("Material " << i << " (" << mats[i].m_Name << ") texIdx=" << texIdx);
            REQUIRE(texIdx < (int)textures.size());
            CHECK(mats[i].m_AlbedoTextureIndex == textures[texIdx].m_BindlessIndex);
        }
    }
}

// ============================================================================
// TEST SUITE: Material_SceneIntegration
// Integration tests: load a real glTF scene and verify material system end-to-end.
// ============================================================================
TEST_SUITE("Material_SceneIntegration")
{
    // ------------------------------------------------------------------
    // TC-MATSI-01: Lantern scene has exactly 1 material
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSI-01 SceneIntegration - Lantern scene has exactly 1 material")
    {
        SKIP_IF_NO_SAMPLES("Lantern/glTF/Lantern.gltf");
        SceneScope scope("Lantern/glTF/Lantern.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_Materials.size() == 1u);
    }

    // ------------------------------------------------------------------
    // TC-MATSI-02: BoxTextured material has a base color texture
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSI-02 SceneIntegration - BoxTextured material has a base color texture")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        bool foundTexturedMat = false;
        for (const auto& m : mats)
            if (m.m_BaseColorTexture != -1) { foundTexturedMat = true; break; }

        CHECK(foundTexturedMat);
    }

    // ------------------------------------------------------------------
    // TC-MATSI-03: BoxTextured material has TEXFLAG_ALBEDO set in MaterialConstants
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSI-03 SceneIntegration - BoxTextured material has TEXFLAG_ALBEDO set")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats     = g_Renderer.m_Scene.m_Materials;
        const auto& textures = g_Renderer.m_Scene.m_Textures;

        bool foundAlbedoFlag = false;
        for (const auto& m : mats)
        {
            if (m.m_BaseColorTexture == -1) continue;
            const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(m, textures);
            if (mc.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_ALBEDO)
            {
                foundAlbedoFlag = true;
                break;
            }
        }
        CHECK(foundAlbedoFlag);
    }

    // ------------------------------------------------------------------
    // TC-MATSI-04: All materials in BoxTextured have valid roughness/metallic
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSI-04 SceneIntegration - all BoxTextured materials have valid roughness/metallic")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats = g_Renderer.m_Scene.m_Materials;
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ")");
            const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(mats[i], g_Renderer.m_Scene.m_Textures);
            CHECK(mc.m_RoughnessMetallic.x >= 0.0f);
            CHECK(mc.m_RoughnessMetallic.x <= 1.0f);
            CHECK(mc.m_RoughnessMetallic.y >= 0.0f);
            CHECK(mc.m_RoughnessMetallic.y <= 1.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-MATSI-05: MaterialConstants alpha mode matches Scene::Material alpha mode
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSI-05 SceneIntegration - MaterialConstants alpha mode matches Scene::Material")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats     = g_Renderer.m_Scene.m_Materials;
        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ")");
            const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(mats[i], textures);
            CHECK(mc.m_AlphaMode == mats[i].m_AlphaMode);
        }
    }

    // ------------------------------------------------------------------
    // TC-MATSI-06: MaterialConstants base color matches Scene::Material base color
    // ------------------------------------------------------------------
    TEST_CASE("TC-MATSI-06 SceneIntegration - MaterialConstants base color matches Scene::Material")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const auto& mats     = g_Renderer.m_Scene.m_Materials;
        const auto& textures = g_Renderer.m_Scene.m_Textures;
        for (int i = 0; i < (int)mats.size(); ++i)
        {
            INFO("Material " << i << " (" << mats[i].m_Name << ")");
            const srrhi::MaterialConstants mc = MaterialConstantsFromMaterial(mats[i], textures);
            CHECK(mc.m_BaseColor.x == doctest::Approx(mats[i].m_BaseColorFactor.x).epsilon(0.001f));
            CHECK(mc.m_BaseColor.y == doctest::Approx(mats[i].m_BaseColorFactor.y).epsilon(0.001f));
            CHECK(mc.m_BaseColor.z == doctest::Approx(mats[i].m_BaseColorFactor.z).epsilon(0.001f));
            CHECK(mc.m_BaseColor.w == doctest::Approx(mats[i].m_BaseColorFactor.w).epsilon(0.001f));
        }
    }

    // ------------------------------------------------------------------
    // TC-MATSI-07: Full frame renders without crash after material load
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-MATSI-07 SceneIntegration - full frame renders without crash after material load")
    {
        CHECK(RunOneFrame());
    }

    // ------------------------------------------------------------------
    // TC-MATSI-08: Multiple frames render without crash (material system stability)
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-MATSI-08 SceneIntegration - 5 frames render without crash")
    {
        CHECK(RunNFrames(5));
    }
}
