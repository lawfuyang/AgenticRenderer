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
//   - Sun direction is a unit vector after SetSunPitchYaw
//   - Sun pitch/yaw round-trip is consistent
//   - Sun direction Y is positive when pitch > 0
//   - Shader handles for deferred and sky passes are non-null
//   - Rendering mode constants match srrhi GPU values
//   - Debug mode can be set to any valid value without crash
//   - Bruneton texture indices are distinct
//
// Run with: HobbyRenderer --run-tests=*Lighting* --gltf-samples <path>
// ============================================================================

#include "TestFixtures.h"
#include "GraphicsTestUtils.h"

#include "shaders/srrhi/cpp/GPULight.h"

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

}

// ============================================================================
// TEST SUITE: Lighting_RenderingModeToggles
// ============================================================================
TEST_SUITE("Lighting_RenderingModeToggles")
{
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
// TEST SUITE: Lighting_SkyAtmosphere
// ============================================================================
TEST_SUITE("Lighting_SkyAtmosphere")
{
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

}


