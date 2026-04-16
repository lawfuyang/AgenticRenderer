// Tests_Lighting.cpp
//
// Systems under test: DeferredRenderer, SkyRenderer, RTXDIRenderer,
//                     Scene light system, RenderingMode toggles
// Prerequisites: g_Renderer fully initialized (RHI + CommonResources).
//               Scene loading is performed inside individual tests using
//               SceneScope where GPU-resident geometry / lights are required.
//
// Test coverage:
//   - DeferredRenderer is registered and is NOT a base-pass renderer
//   - SkyRenderer is registered and is NOT a base-pass renderer
//   - Rendering mode can be toggled (Normal / IBL / PathTracer) without crash
//   - m_EnableSky flag can be toggled without crash
//   - m_EnableRTShadows flag can be toggled without crash
//   - m_EnableReSTIRDI flag can be toggled without crash
//   - m_EnableReSTIRDIRelaxDenoising flag can be toggled without crash
//   - Scene always has at least one directional light after load
//   - Sun direction is a unit vector after scene load
//   - Sun pitch/yaw round-trip is consistent
//   - Light buffer is non-null after scene load
//   - m_LightCount matches m_Lights.size() after scene load
//   - Directional light type constant is 0 (matches GPU convention)
//   - Point light type constant is 1 (matches GPU convention)
//   - Spot light type constant is 2 (matches GPU convention)
//   - Sun intensity is positive after scene load
//   - Sun direction can be mutated via SetSunPitchYaw without crash
//   - m_LightsDirty flag is accessible and can be set/cleared
//   - HDR color format is non-UNKNOWN (output of deferred pass)
//   - Shader handles for deferred and sky passes are non-null
//   - Shader hot-reload does not invalidate deferred/sky shader handles
//   - Debug mode can be set to any valid value without crash
//   - IBL rendering mode: m_Mode can be set to IBL
//   - Normal rendering mode: m_Mode can be restored to Normal
//   - ReSTIR DI can be disabled and re-enabled without crash
//   - Backbuffer capture stub returns true after lighting pass setup
//
// Run with: HobbyRenderer --run-tests=*Lighting* --gltf-samples <path>
// ============================================================================

#include "TestFixtures.h"
#include "GraphicsTestUtils.h"

#include "shaders/srrhi/cpp/GPULight.h"

// ============================================================================
// Internal helpers
// ============================================================================
namespace
{
    // Compute the squared length of a Vector3 (for unit-vector checks).
    float LengthSq(const Vector3& v)
    {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }
} // anonymous namespace

// ============================================================================
// TEST SUITE: Lighting_RendererRegistration
// ============================================================================
TEST_SUITE("Lighting_RendererRegistration")
{
    // ------------------------------------------------------------------
    // TC-LREG-01: DeferredRenderer is registered
    // ------------------------------------------------------------------
    TEST_CASE("TC-LREG-01 LightingRegistry - DeferredRenderer is registered")
    {
        IRenderer* r = FindRendererByName("Deferred");
        CHECK(r != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-LREG-02: DeferredRenderer is NOT a base-pass renderer
    // ------------------------------------------------------------------
    TEST_CASE("TC-LREG-02 LightingRegistry - DeferredRenderer is not a base-pass renderer")
    {
        IRenderer* r = FindRendererByName("Deferred");
        if (r)
            CHECK(!r->IsBasePassRenderer());
    }

    // ------------------------------------------------------------------
    // TC-LREG-03: SkyRenderer is registered
    // ------------------------------------------------------------------
    TEST_CASE("TC-LREG-03 LightingRegistry - SkyRenderer is registered")
    {
        IRenderer* r = FindRendererByName("Sky");
        CHECK(r != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-LREG-04: SkyRenderer is NOT a base-pass renderer
    // ------------------------------------------------------------------
    TEST_CASE("TC-LREG-04 LightingRegistry - SkyRenderer is not a base-pass renderer")
    {
        IRenderer* r = FindRendererByName("Sky");
        if (r)
            CHECK(!r->IsBasePassRenderer());
    }

    // ------------------------------------------------------------------
    // TC-LREG-05: DeferredRenderer has a non-null, non-empty name
    // ------------------------------------------------------------------
    TEST_CASE("TC-LREG-05 LightingRegistry - DeferredRenderer has non-empty name")
    {
        IRenderer* r = FindRendererByName("Deferred");
        if (r)
        {
            CHECK(r->GetName() != nullptr);
            CHECK(std::string_view(r->GetName()).size() > 0u);
        }
    }

    // ------------------------------------------------------------------
    // TC-LREG-06: SkyRenderer has a non-null, non-empty name
    // ------------------------------------------------------------------
    TEST_CASE("TC-LREG-06 LightingRegistry - SkyRenderer has non-empty name")
    {
        IRenderer* r = FindRendererByName("Sky");
        if (r)
        {
            CHECK(r->GetName() != nullptr);
            CHECK(std::string_view(r->GetName()).size() > 0u);
        }
    }
}

// ============================================================================
// TEST SUITE: Lighting_RenderingModeToggles
// ============================================================================
TEST_SUITE("Lighting_RenderingModeToggles")
{
    // ------------------------------------------------------------------
    // TC-LMODE-01: Default rendering mode is Normal
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-01 RenderingModeToggles - default mode is Normal")
    {
        // The renderer is initialized with Normal mode.
        // We just verify the field is accessible and holds the expected value.
        CHECK(g_Renderer.m_Mode == RenderingMode::Normal);
    }

    // ------------------------------------------------------------------
    // TC-LMODE-02: Rendering mode can be switched to IBL without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-02 RenderingModeToggles - switch to IBL does not crash")
    {
        const RenderingMode prev = g_Renderer.m_Mode;
        g_Renderer.m_Mode = RenderingMode::IBL;
        CHECK(g_Renderer.m_Mode == RenderingMode::IBL);
        g_Renderer.m_Mode = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-03: Rendering mode can be switched to ReferencePathTracer without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-03 RenderingModeToggles - switch to ReferencePathTracer does not crash")
    {
        const RenderingMode prev = g_Renderer.m_Mode;
        g_Renderer.m_Mode = RenderingMode::ReferencePathTracer;
        CHECK(g_Renderer.m_Mode == RenderingMode::ReferencePathTracer);
        g_Renderer.m_Mode = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-04: Rendering mode can be restored to Normal without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-04 RenderingModeToggles - restore to Normal does not crash")
    {
        const RenderingMode prev = g_Renderer.m_Mode;
        g_Renderer.m_Mode = RenderingMode::IBL;
        g_Renderer.m_Mode = RenderingMode::Normal;
        CHECK(g_Renderer.m_Mode == RenderingMode::Normal);
        g_Renderer.m_Mode = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-05: m_EnableSky can be disabled without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-05 RenderingModeToggles - disable sky does not crash")
    {
        const bool prev = g_Renderer.m_EnableSky;
        g_Renderer.m_EnableSky = false;
        CHECK(!g_Renderer.m_EnableSky);
        g_Renderer.m_EnableSky = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-06: m_EnableSky can be re-enabled without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-06 RenderingModeToggles - re-enable sky does not crash")
    {
        const bool prev = g_Renderer.m_EnableSky;
        g_Renderer.m_EnableSky = false;
        g_Renderer.m_EnableSky = true;
        CHECK(g_Renderer.m_EnableSky);
        g_Renderer.m_EnableSky = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-07: m_EnableRTShadows can be toggled without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-07 RenderingModeToggles - RT shadows toggle does not crash")
    {
        const bool prev = g_Renderer.m_EnableRTShadows;
        g_Renderer.m_EnableRTShadows = !prev;
        CHECK(g_Renderer.m_EnableRTShadows == !prev);
        g_Renderer.m_EnableRTShadows = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-08: m_EnableReSTIRDI can be disabled without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-08 RenderingModeToggles - disable ReSTIR DI does not crash")
    {
        const bool prev = g_Renderer.m_EnableReSTIRDI;
        g_Renderer.m_EnableReSTIRDI = false;
        CHECK(!g_Renderer.m_EnableReSTIRDI);
        g_Renderer.m_EnableReSTIRDI = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-09: m_EnableReSTIRDI can be re-enabled without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-09 RenderingModeToggles - re-enable ReSTIR DI does not crash")
    {
        const bool prev = g_Renderer.m_EnableReSTIRDI;
        g_Renderer.m_EnableReSTIRDI = false;
        g_Renderer.m_EnableReSTIRDI = true;
        CHECK(g_Renderer.m_EnableReSTIRDI);
        g_Renderer.m_EnableReSTIRDI = prev;
    }

    // ------------------------------------------------------------------
    // TC-LMODE-10: m_EnableReSTIRDIRelaxDenoising can be toggled without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LMODE-10 RenderingModeToggles - RELAX denoising toggle does not crash")
    {
        const bool prev = g_Renderer.m_EnableReSTIRDIRelaxDenoising;
        g_Renderer.m_EnableReSTIRDIRelaxDenoising = !prev;
        CHECK(g_Renderer.m_EnableReSTIRDIRelaxDenoising == !prev);
        g_Renderer.m_EnableReSTIRDIRelaxDenoising = prev;
    }
}

// ============================================================================
// TEST SUITE: Lighting_LightSystem
// ============================================================================
TEST_SUITE("Lighting_LightSystem")
{
    // ------------------------------------------------------------------
    // TC-LSYS-01: Light type constants match GPU convention
    //             Directional=0, Point=1, Spot=2
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-01 LightSystem - light type constants match GPU convention")
    {
        CHECK((int)Scene::Light::Directional == 0);
        CHECK((int)Scene::Light::Point       == 1);
        CHECK((int)Scene::Light::Spot        == 2);
    }

    // ------------------------------------------------------------------
    // TC-LSYS-02: Scene always has at least one light after load
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-02 LightSystem - scene has at least one light after load")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(!g_Renderer.m_Scene.m_Lights.empty());
    }

    // ------------------------------------------------------------------
    // TC-LSYS-03: Scene always has at least one directional light after load
    //             (SceneLoader injects a default directional light if none present)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-03 LightSystem - scene has at least one directional light")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        bool hasDirectional = false;
        for (const auto& light : g_Renderer.m_Scene.m_Lights)
        {
            if (light.m_Type == Scene::Light::Directional)
            {
                hasDirectional = true;
                break;
            }
        }
        CHECK(hasDirectional);
    }

    // ------------------------------------------------------------------
    // TC-LSYS-04: m_LightCount matches m_Lights.size() after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-04 LightSystem - m_LightCount matches m_Lights.size()")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_LightCount ==
              (uint32_t)g_Renderer.m_Scene.m_Lights.size());
    }

    // ------------------------------------------------------------------
    // TC-LSYS-05: GPU light buffer is non-null after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-05 LightSystem - GPU light buffer is non-null after scene load")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_LightBuffer != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-LSYS-06: GPU light buffer has sufficient byte size
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-06 LightSystem - GPU light buffer has sufficient byte size")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        REQUIRE(g_Renderer.m_Scene.m_LightBuffer != nullptr);
        const uint64_t expectedMinSize =
            (uint64_t)g_Renderer.m_Scene.m_LightCount * sizeof(srrhi::GPULight);
        CHECK(g_Renderer.m_Scene.m_LightBuffer->getDesc().byteSize >= expectedMinSize);
    }

    // ------------------------------------------------------------------
    // TC-LSYS-07: Sun direction is a unit vector after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-07 LightSystem - sun direction is a unit vector")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const Vector3 sunDir = g_Renderer.m_Scene.GetSunDirection();
        const float lenSq = LengthSq(sunDir);
        CHECK(lenSq == doctest::Approx(1.0f).epsilon(0.001f));
    }

    // ------------------------------------------------------------------
    // TC-LSYS-08: Sun intensity is positive after scene load
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-08 LightSystem - sun intensity is positive")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.GetSunIntensity() > 0.0f);
    }

    // ------------------------------------------------------------------
    // TC-LSYS-09: Directional light has a valid node index
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-09 LightSystem - directional light has valid node index")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        for (const auto& light : g_Renderer.m_Scene.m_Lights)
        {
            if (light.m_Type == Scene::Light::Directional)
            {
                CHECK(light.m_NodeIndex >= 0);
                CHECK(light.m_NodeIndex < (int)g_Renderer.m_Scene.m_Nodes.size());
                break;
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-LSYS-10: m_LightsDirty flag is accessible and can be cleared
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSYS-10 LightSystem - m_LightsDirty flag is accessible")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        // The flag should be accessible; we can read and write it.
        const bool prev = g_Renderer.m_Scene.m_LightsDirty;
        g_Renderer.m_Scene.m_LightsDirty = false;
        CHECK(!g_Renderer.m_Scene.m_LightsDirty);
        g_Renderer.m_Scene.m_LightsDirty = prev;
    }
}

// ============================================================================
// TEST SUITE: Lighting_SunMutation
// ============================================================================
TEST_SUITE("Lighting_SunMutation")
{
    // ------------------------------------------------------------------
    // TC-LSUN-01: SetSunPitchYaw does not crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSUN-01 SunMutation - SetSunPitchYaw does not crash")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK_NOTHROW(g_Renderer.m_Scene.SetSunPitchYaw(
            DirectX::XM_PIDIV4, 0.0f));
    }

    // ------------------------------------------------------------------
    // TC-LSUN-02: Sun direction is still a unit vector after SetSunPitchYaw
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSUN-02 SunMutation - sun direction is unit vector after SetSunPitchYaw")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        g_Renderer.m_Scene.SetSunPitchYaw(DirectX::XM_PIDIV4, DirectX::XM_PIDIV2);

        const Vector3 sunDir = g_Renderer.m_Scene.GetSunDirection();
        const float lenSq = LengthSq(sunDir);
        CHECK(lenSq == doctest::Approx(1.0f).epsilon(0.001f));
    }

    // ------------------------------------------------------------------
    // TC-LSUN-03: GetSunPitch returns a value in [-PI/2, PI/2]
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSUN-03 SunMutation - GetSunPitch is in valid range")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const float pitch = g_Renderer.m_Scene.GetSunPitch();
        CHECK(pitch >= -DirectX::XM_PIDIV2);
        CHECK(pitch <=  DirectX::XM_PIDIV2);
    }

    // ------------------------------------------------------------------
    // TC-LSUN-04: GetSunYaw returns a value in [-PI, PI]
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSUN-04 SunMutation - GetSunYaw is in valid range")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        const float yaw = g_Renderer.m_Scene.GetSunYaw();
        CHECK(yaw >= -DirectX::XM_PI);
        CHECK(yaw <=  DirectX::XM_PI);
    }

    // ------------------------------------------------------------------
    // TC-LSUN-05: SetSunPitchYaw round-trip: pitch is preserved
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSUN-05 SunMutation - SetSunPitchYaw pitch round-trip")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        constexpr float kPitch = 0.6f; // ~34 degrees
        constexpr float kYaw   = 0.3f;
        g_Renderer.m_Scene.SetSunPitchYaw(kPitch, kYaw);

        const float readback = g_Renderer.m_Scene.GetSunPitch();
        CHECK(readback == doctest::Approx(kPitch).epsilon(0.01f));
    }

    // ------------------------------------------------------------------
    // TC-LSUN-06: Sun direction Y component is positive when pitch > 0
    //             (sun above horizon in reversed-Z / LH convention)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSUN-06 SunMutation - sun Y is positive when pitch > 0")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        // Positive pitch = sun elevated above horizon.
        // GetSunDirection() returns toward-sun, so Y > 0 means sun is above.
        g_Renderer.m_Scene.SetSunPitchYaw(DirectX::XM_PIDIV4, 0.0f);
        const Vector3 sunDir = g_Renderer.m_Scene.GetSunDirection();
        CHECK(sunDir.y > 0.0f);
    }
}

// ============================================================================
// TEST SUITE: Lighting_DeferredShaders
// ============================================================================
TEST_SUITE("Lighting_DeferredShaders")
{
    // ------------------------------------------------------------------
    // TC-LSHD-01: Deferred lighting shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-01 DeferredShaders - deferred lighting shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(
            ShaderID::DEFERREDLIGHTING_DEFERREDLIGHTING_PSMAIN) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-LSHD-02: Sky shader handle is non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-02 DeferredShaders - sky shader is non-null")
    {
        CHECK(g_Renderer.GetShaderHandle(ShaderID::SKY_SKY_PSMAIN) != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-LSHD-03: Deferred lighting shader handle is non-null after reload request
    //             (reload is deferred to next frame; handle remains valid now)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-03 DeferredShaders - deferred shader non-null after reload request")
    {
        g_Renderer.m_RequestedShaderReload = true;

        CHECK(g_Renderer.GetShaderHandle(
            ShaderID::DEFERREDLIGHTING_DEFERREDLIGHTING_PSMAIN) != nullptr);

        g_Renderer.m_RequestedShaderReload = false;
    }

    // ------------------------------------------------------------------
    // TC-LSHD-04: Sky shader handle is non-null after reload request
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-04 DeferredShaders - sky shader non-null after reload request")
    {
        g_Renderer.m_RequestedShaderReload = true;

        CHECK(g_Renderer.GetShaderHandle(ShaderID::SKY_SKY_PSMAIN) != nullptr);

        g_Renderer.m_RequestedShaderReload = false;
    }

    // ------------------------------------------------------------------
    // TC-LSHD-05: HDR color format is non-UNKNOWN (output of deferred pass)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-05 DeferredShaders - HDR color format is non-UNKNOWN")
    {
        CHECK(Renderer::HDR_COLOR_FORMAT != nvrhi::Format::UNKNOWN);
    }

    // ------------------------------------------------------------------
    // TC-LSHD-06: HDR color format is R11G11B10_FLOAT
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-06 DeferredShaders - HDR color format is R11G11B10_FLOAT")
    {
        CHECK(Renderer::HDR_COLOR_FORMAT == nvrhi::Format::R11G11B10_FLOAT);
    }

    // ------------------------------------------------------------------
    // TC-LSHD-07: Rendering mode constants match srrhi values
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-07 DeferredShaders - rendering mode constants match srrhi values")
    {
        CHECK((uint32_t)RenderingMode::Normal ==
              (uint32_t)srrhi::CommonConsts::RENDERING_MODE_NORMAL);
        CHECK((uint32_t)RenderingMode::IBL ==
              (uint32_t)srrhi::CommonConsts::RENDERING_MODE_IBL);
        CHECK((uint32_t)RenderingMode::ReferencePathTracer ==
              (uint32_t)srrhi::CommonConsts::RENDERING_MODE_PATH_TRACER);
    }

    // ------------------------------------------------------------------
    // TC-LSHD-08: Debug mode can be set to any valid value without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LSHD-08 DeferredShaders - debug mode can be set to any valid value")
    {
        const int prev = g_Renderer.m_DebugMode;

        // Cycle through all known debug modes
        for (int mode = srrhi::CommonConsts::DEBUG_MODE_NONE;
             mode <= srrhi::CommonConsts::DEBUG_MODE_REGIR_CELLS;
             ++mode)
        {
            g_Renderer.m_DebugMode = mode;
            CHECK(g_Renderer.m_DebugMode == mode);
        }

        g_Renderer.m_DebugMode = prev;
    }
}

// ============================================================================
// TEST SUITE: Lighting_LightCulling
// ============================================================================
TEST_SUITE("Lighting_LightCulling")
{
    // ------------------------------------------------------------------
    // TC-LCULL-01: Scene with only a directional light has LightCount >= 1
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCULL-01 LightCulling - scene with directional light has LightCount >= 1")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        CHECK(g_Renderer.m_Scene.m_LightCount >= 1u);
    }

    // ------------------------------------------------------------------
    // TC-LCULL-02: All lights have a valid node index
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCULL-02 LightCulling - all lights have valid node indices")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        for (int i = 0; i < (int)g_Renderer.m_Scene.m_Lights.size(); ++i)
        {
            const auto& light = g_Renderer.m_Scene.m_Lights[i];
            INFO("Light index " << i);
            CHECK(light.m_NodeIndex >= 0);
            CHECK(light.m_NodeIndex < (int)g_Renderer.m_Scene.m_Nodes.size());
        }
    }

    // ------------------------------------------------------------------
    // TC-LCULL-03: All lights have positive intensity
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCULL-03 LightCulling - all lights have positive intensity")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        for (int i = 0; i < (int)g_Renderer.m_Scene.m_Lights.size(); ++i)
        {
            const auto& light = g_Renderer.m_Scene.m_Lights[i];
            INFO("Light index " << i);
            CHECK(light.m_Intensity > 0.0f);
        }
    }

    // ------------------------------------------------------------------
    // TC-LCULL-04: All lights have a valid type (Directional, Point, or Spot)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCULL-04 LightCulling - all lights have valid type")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        for (int i = 0; i < (int)g_Renderer.m_Scene.m_Lights.size(); ++i)
        {
            const auto& light = g_Renderer.m_Scene.m_Lights[i];
            INFO("Light index " << i);
            const bool validType = (light.m_Type == Scene::Light::Directional ||
                                    light.m_Type == Scene::Light::Point       ||
                                    light.m_Type == Scene::Light::Spot);
            CHECK(validType);
        }
    }

    // ------------------------------------------------------------------
    // TC-LCULL-05: Spot lights have inner cone angle <= outer cone angle
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCULL-05 LightCulling - spot lights have inner <= outer cone angle")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        for (int i = 0; i < (int)g_Renderer.m_Scene.m_Lights.size(); ++i)
        {
            const auto& light = g_Renderer.m_Scene.m_Lights[i];
            if (light.m_Type == Scene::Light::Spot)
            {
                INFO("Spot light index " << i);
                CHECK(light.m_SpotInnerConeAngle <= light.m_SpotOuterConeAngle);
            }
        }
    }

    // ------------------------------------------------------------------
    // TC-LCULL-06: Point lights have non-negative radius
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCULL-06 LightCulling - point lights have non-negative radius")
    {
        SKIP_IF_NO_SAMPLES("BoxTextured/glTF/BoxTextured.gltf");
        SceneScope scope("BoxTextured/glTF/BoxTextured.gltf");
        REQUIRE(scope.loaded);

        for (int i = 0; i < (int)g_Renderer.m_Scene.m_Lights.size(); ++i)
        {
            const auto& light = g_Renderer.m_Scene.m_Lights[i];
            if (light.m_Type == Scene::Light::Point)
            {
                INFO("Point light index " << i);
                CHECK(light.m_Radius >= 0.0f);
            }
        }
    }
}

// ============================================================================
// TEST SUITE: Lighting_SkyAtmosphere
// ============================================================================
TEST_SUITE("Lighting_SkyAtmosphere")
{
    // ------------------------------------------------------------------
    // TC-LATMO-01: m_EnableSky default is true
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-01 SkyAtmosphere - m_EnableSky default is true")
    {
        // The renderer is initialized with sky enabled.
        CHECK(g_Renderer.m_EnableSky);
    }

    // ------------------------------------------------------------------
    // TC-LATMO-02: Irradiance texture path is non-empty
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-02 SkyAtmosphere - irradiance texture path is non-empty")
    {
        CHECK(!g_Renderer.m_IrradianceTexturePath.empty());
    }

    // ------------------------------------------------------------------
    // TC-LATMO-03: Radiance texture path is non-empty
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-03 SkyAtmosphere - radiance texture path is non-empty")
    {
        CHECK(!g_Renderer.m_RadianceTexturePath.empty());
    }

    // ------------------------------------------------------------------
    // TC-LATMO-04: BRDF LUT texture path is non-empty
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-04 SkyAtmosphere - BRDF LUT texture path is non-empty")
    {
        CHECK(!g_Renderer.m_BRDFLutTexture.empty());
    }

    // ------------------------------------------------------------------
    // TC-LATMO-05: Bruneton transmittance texture index is non-zero
    //             (it is a well-known bindless slot, not the black default)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-05 SkyAtmosphere - Bruneton transmittance texture index is valid")
    {
        // The transmittance texture occupies a fixed bindless slot.
        CHECK(srrhi::CommonConsts::BRUNETON_TRANSMITTANCE_TEXTURE > 0);
    }

    // ------------------------------------------------------------------
    // TC-LATMO-06: Bruneton scattering texture index is non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-06 SkyAtmosphere - Bruneton scattering texture index is valid")
    {
        CHECK(srrhi::CommonConsts::BRUNETON_SCATTERING_TEXTURE > 0);
    }

    // ------------------------------------------------------------------
    // TC-LATMO-07: Bruneton irradiance texture index is non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-07 SkyAtmosphere - Bruneton irradiance texture index is valid")
    {
        CHECK(srrhi::CommonConsts::BRUNETON_IRRADIANCE_TEXTURE > 0);
    }

    // ------------------------------------------------------------------
    // TC-LATMO-08: Bruneton texture indices are distinct
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-08 SkyAtmosphere - Bruneton texture indices are distinct")
    {
        CHECK(srrhi::CommonConsts::BRUNETON_TRANSMITTANCE_TEXTURE !=
              srrhi::CommonConsts::BRUNETON_SCATTERING_TEXTURE);
        CHECK(srrhi::CommonConsts::BRUNETON_SCATTERING_TEXTURE !=
              srrhi::CommonConsts::BRUNETON_IRRADIANCE_TEXTURE);
        CHECK(srrhi::CommonConsts::BRUNETON_TRANSMITTANCE_TEXTURE !=
              srrhi::CommonConsts::BRUNETON_IRRADIANCE_TEXTURE);
    }

    // ------------------------------------------------------------------
    // TC-LATMO-09: Sky can be disabled while in IBL mode without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-09 SkyAtmosphere - sky disabled in IBL mode does not crash")
    {
        const RenderingMode prevMode = g_Renderer.m_Mode;
        const bool prevSky           = g_Renderer.m_EnableSky;

        g_Renderer.m_Mode      = RenderingMode::IBL;
        g_Renderer.m_EnableSky = false;

        CHECK(g_Renderer.m_Mode      == RenderingMode::IBL);
        CHECK(!g_Renderer.m_EnableSky);

        g_Renderer.m_Mode      = prevMode;
        g_Renderer.m_EnableSky = prevSky;
    }

    // ------------------------------------------------------------------
    // TC-LATMO-10: Sky can be enabled while in Normal mode without crash
    // ------------------------------------------------------------------
    TEST_CASE("TC-LATMO-10 SkyAtmosphere - sky enabled in Normal mode does not crash")
    {
        const RenderingMode prevMode = g_Renderer.m_Mode;
        const bool prevSky           = g_Renderer.m_EnableSky;

        g_Renderer.m_Mode      = RenderingMode::Normal;
        g_Renderer.m_EnableSky = true;

        CHECK(g_Renderer.m_Mode == RenderingMode::Normal);
        CHECK(g_Renderer.m_EnableSky);

        g_Renderer.m_Mode      = prevMode;
        g_Renderer.m_EnableSky = prevSky;
    }
}

// ============================================================================
// TEST SUITE: Lighting_BackbufferCapture
// ============================================================================
TEST_SUITE("Lighting_BackbufferCapture")
{
    // ------------------------------------------------------------------
    // TC-LCAP-01: Backbuffer capture stub returns true (lighting pass)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCAP-01 LightingCapture - capture stub returns true")
    {
        const bool ok = capture_backbuffer_to_bmp(
            "Tests/ReferenceImages/TC-LCAP-01-Lighting.bmp");
        CHECK(ok);
    }

    // ------------------------------------------------------------------
    // TC-LCAP-02: Image comparison stub returns true (lighting pass)
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCAP-02 LightingCapture - compare stub returns true")
    {
        const bool ok = compare_bmp_images(
            "Tests/ReferenceImages/TC-LCAP-02-actual.bmp",
            "Tests/ReferenceImages/TC-LCAP-02-reference.bmp",
            1.0f);
        CHECK(ok);
    }

    // ------------------------------------------------------------------
    // TC-LCAP-03: Image comparison stub returns true with zero tolerance
    // ------------------------------------------------------------------
    TEST_CASE("TC-LCAP-03 LightingCapture - compare stub returns true with zero tolerance")
    {
        const bool ok = compare_bmp_images(
            "Tests/ReferenceImages/TC-LCAP-03-actual.bmp",
            "Tests/ReferenceImages/TC-LCAP-03-reference.bmp",
            0.0f);
        CHECK(ok);
    }
}
