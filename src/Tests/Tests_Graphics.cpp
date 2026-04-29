// Tests_Graphics.cpp — Graphics Infrastructure Tests
//
// Systems under test: D3D12RHI, CommonResources
// Prerequisites: g_Renderer fully initialized (RHI, bindless system, CommonResources)
//                This is guaranteed when RunTests() is called from Renderer.cpp.
//
// Run with: HobbyRenderer --run-tests=*Graphics*
// ============================================================================

#include "TestFixtures.h"

// ============================================================================
// TEST SUITE: D3D12RHI — Device & Swapchain
// ============================================================================
TEST_SUITE("Graphics_RHI")
{
    // ------------------------------------------------------------------
    // TC-RHI-01: NVRHI device is non-null after initialization
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-01 Device — NVRHI device handle is valid")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        CHECK(DEV() != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-RHI-02: Graphics API is D3D12
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-02 Device — graphics API is D3D12")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        CHECK(g_Renderer.m_RHI->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12);
    }

    // ------------------------------------------------------------------
    // TC-RHI-03: Swapchain textures are non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-03 Swapchain — all swapchain texture handles are valid")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        for (uint32_t i = 0; i < GraphicRHI::SwapchainImageCount; ++i)
        {
            INFO("Swapchain image index: " << i);
            CHECK(g_Renderer.m_RHI->m_NvrhiSwapchainTextures[i] != nullptr);
        }
    }

    // ------------------------------------------------------------------
    // TC-RHI-04: Swapchain format is RGBA8_UNORM
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-04 Swapchain — format is RGBA8_UNORM")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        CHECK(g_Renderer.m_RHI->m_SwapchainFormat == nvrhi::Format::RGBA8_UNORM);
    }

    // ------------------------------------------------------------------
    // TC-RHI-05: Swapchain extent is non-zero
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-05 Swapchain — extent width and height are non-zero")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        CHECK(g_Renderer.m_RHI->m_SwapchainExtent.x > 0u);
        CHECK(g_Renderer.m_RHI->m_SwapchainExtent.y > 0u);
    }

    // ------------------------------------------------------------------
    // TC-RHI-06: Swapchain textures match the declared swapchain extent
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-06 Swapchain — texture dimensions match swapchain extent")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        const uint32_t w = g_Renderer.m_RHI->m_SwapchainExtent.x;
        const uint32_t h = g_Renderer.m_RHI->m_SwapchainExtent.y;

        for (uint32_t i = 0; i < GraphicRHI::SwapchainImageCount; ++i)
        {
            REQUIRE(g_Renderer.m_RHI->m_NvrhiSwapchainTextures[i] != nullptr);
            const nvrhi::TextureDesc& desc = g_Renderer.m_RHI->m_NvrhiSwapchainTextures[i]->getDesc();
            INFO("Swapchain image index: " << i);
            CHECK(desc.width  == w);
            CHECK(desc.height == h);
        }
    }

    // ------------------------------------------------------------------
    // TC-RHI-07: Required D3D12 features are supported
    //            (HeapDirectlyIndexed, Meshlets, RayQuery, RTAS)
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-07 Device — required feature support")
    {
        REQUIRE(DEV() != nullptr);
        CHECK(DEV()->queryFeatureSupport(nvrhi::Feature::HeapDirectlyIndexed));
        CHECK(DEV()->queryFeatureSupport(nvrhi::Feature::Meshlets));
        CHECK(DEV()->queryFeatureSupport(nvrhi::Feature::RayQuery));
        CHECK(DEV()->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct));
    }

    // ------------------------------------------------------------------
    // TC-RHI-08: VRAM usage query returns a non-negative value
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-08 Device — VRAM usage query returns non-negative value")
    {
        REQUIRE(g_Renderer.m_RHI != nullptr);
        const float vramMB = g_Renderer.m_RHI->GetVRAMUsageMB();
        CHECK(vramMB >= 0.0f);
    }

    // ------------------------------------------------------------------
    // TC-RHI-09: Command list can be created and executed without error
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-09 CommandList — create and execute a no-op command list")
    {
        REQUIRE(DEV() != nullptr);

        nvrhi::CommandListHandle cmd = DEV()->createCommandList();
        REQUIRE(cmd != nullptr);

        cmd->open();
        // No-op: just open and close
        cmd->close();

        g_Renderer.ExecutePendingCommandLists();
        DEV()->waitForIdle();

        CHECK(true); // Reached here without crash/assert
    }

    // ------------------------------------------------------------------
    // TC-RHI-10: Timer query can be created
    // ------------------------------------------------------------------
    TEST_CASE("TC-RHI-10 TimerQuery — timer query handle is valid")
    {
        REQUIRE(DEV() != nullptr);
        nvrhi::TimerQueryHandle query = DEV()->createTimerQuery();
        CHECK(query != nullptr);
    }
}

// ============================================================================
// TEST SUITE: CommonResources — Samplers
// ============================================================================
TEST_SUITE("Graphics_Samplers")
{
    // ------------------------------------------------------------------
    // TC-SAMP-02: LinearClamp has correct address mode (ClampToEdge)
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-02 Samplers — LinearClamp address mode is ClampToEdge")
    {
        REQUIRE(CR().LinearClamp != nullptr);
        const nvrhi::SamplerDesc& desc = CR().LinearClamp->getDesc();
        CHECK(desc.addressU == nvrhi::SamplerAddressMode::ClampToEdge);
        CHECK(desc.addressV == nvrhi::SamplerAddressMode::ClampToEdge);
        CHECK(desc.addressW == nvrhi::SamplerAddressMode::ClampToEdge);
    }

    // ------------------------------------------------------------------
    // TC-SAMP-03: LinearWrap has correct address mode (Wrap)
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-03 Samplers — LinearWrap address mode is Wrap")
    {
        REQUIRE(CR().LinearWrap != nullptr);
        const nvrhi::SamplerDesc& desc = CR().LinearWrap->getDesc();
        CHECK(desc.addressU == nvrhi::SamplerAddressMode::Wrap);
        CHECK(desc.addressV == nvrhi::SamplerAddressMode::Wrap);
        CHECK(desc.addressW == nvrhi::SamplerAddressMode::Wrap);
    }

    // ------------------------------------------------------------------
    // TC-SAMP-04: PointClamp has minFilter/magFilter = false (point)
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-04 Samplers — PointClamp uses point filtering")
    {
        REQUIRE(CR().PointClamp != nullptr);
        const nvrhi::SamplerDesc& desc = CR().PointClamp->getDesc();
        CHECK_FALSE(desc.minFilter);
        CHECK_FALSE(desc.magFilter);
    }

    // ------------------------------------------------------------------
    // TC-SAMP-05: AnisotropicClamp has maxAnisotropy = 16
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-05 Samplers — AnisotropicClamp maxAnisotropy is 16")
    {
        REQUIRE(CR().AnisotropicClamp != nullptr);
        const nvrhi::SamplerDesc& desc = CR().AnisotropicClamp->getDesc();
        CHECK(desc.maxAnisotropy == doctest::Approx(16.0f));
    }

    // ------------------------------------------------------------------
    // TC-SAMP-06: AnisotropicWrap has maxAnisotropy = 16 and Wrap mode
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-06 Samplers — AnisotropicWrap maxAnisotropy is 16 and Wrap mode")
    {
        REQUIRE(CR().AnisotropicWrap != nullptr);
        const nvrhi::SamplerDesc& desc = CR().AnisotropicWrap->getDesc();
        CHECK(desc.maxAnisotropy == doctest::Approx(16.0f));
        CHECK(desc.addressU == nvrhi::SamplerAddressMode::Wrap);
    }

    // ------------------------------------------------------------------
    // TC-SAMP-07: MaxReductionClamp uses Maximum reduction type
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-07 Samplers — MaxReductionClamp uses Maximum reduction")
    {
        REQUIRE(CR().MaxReductionClamp != nullptr);
        const nvrhi::SamplerDesc& desc = CR().MaxReductionClamp->getDesc();
        CHECK(desc.reductionType == nvrhi::SamplerReductionType::Maximum);
    }

    // ------------------------------------------------------------------
    // TC-SAMP-08: MinReductionClamp uses Minimum reduction type
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-08 Samplers — MinReductionClamp uses Minimum reduction")
    {
        REQUIRE(CR().MinReductionClamp != nullptr);
        const nvrhi::SamplerDesc& desc = CR().MinReductionClamp->getDesc();
        CHECK(desc.reductionType == nvrhi::SamplerReductionType::Minimum);
    }

    // ------------------------------------------------------------------
    // TC-SAMP-09: LinearClampBorderWhite uses Border address mode
    // ------------------------------------------------------------------
    TEST_CASE("TC-SAMP-09 Samplers — LinearClampBorderWhite uses Border address mode")
    {
        REQUIRE(CR().LinearClampBorderWhite != nullptr);
        const nvrhi::SamplerDesc& desc = CR().LinearClampBorderWhite->getDesc();
        CHECK(desc.addressU == nvrhi::SamplerAddressMode::Border);
        CHECK(desc.addressV == nvrhi::SamplerAddressMode::Border);
        CHECK(desc.addressW == nvrhi::SamplerAddressMode::Border);
        // Border color should be white (1,1,1,1)
        CHECK(desc.borderColor.r == doctest::Approx(1.0f));
        CHECK(desc.borderColor.g == doctest::Approx(1.0f));
        CHECK(desc.borderColor.b == doctest::Approx(1.0f));
        CHECK(desc.borderColor.a == doctest::Approx(1.0f));
    }
}

// ============================================================================
// TEST SUITE: CommonResources — Default Textures
// ============================================================================
TEST_SUITE("Graphics_DefaultTextures")
{
    // ------------------------------------------------------------------
    // TC-TEX-05: DefaultTexture3DWhite is a Texture3D
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-05 DefaultTextures — 3DWhite is a Texture3D")
    {
        REQUIRE(CR().DefaultTexture3DWhite != nullptr);
        const nvrhi::TextureDesc& desc = CR().DefaultTexture3DWhite->getDesc();
        CHECK(desc.dimension == nvrhi::TextureDimension::Texture3D);
    }

    // ------------------------------------------------------------------
    // TC-TEX-09: IrradianceTexture is a TextureCube
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-09 DefaultTextures — IrradianceTexture is a TextureCube")
    {
        REQUIRE(CR().IrradianceTexture != nullptr);
        const nvrhi::TextureDesc& desc = CR().IrradianceTexture->getDesc();
        CHECK(desc.dimension == nvrhi::TextureDimension::TextureCube);
    }

    // ------------------------------------------------------------------
    // TC-TEX-10: RadianceTexture is a TextureCube with at least 1 mip level
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-10 DefaultTextures — RadianceTexture is a TextureCube with mips")
    {
        REQUIRE(CR().RadianceTexture != nullptr);
        const nvrhi::TextureDesc& desc = CR().RadianceTexture->getDesc();
        CHECK(desc.dimension  == nvrhi::TextureDimension::TextureCube);
        CHECK(desc.mipLevels  >= 1u);
        // m_RadianceMipCount should match the texture's mip count
        CHECK(CR().m_RadianceMipCount == desc.mipLevels);
    }

    // ------------------------------------------------------------------
    // TC-TEX-11: BrunetonScattering is a Texture3D
    // ------------------------------------------------------------------
    TEST_CASE("TC-TEX-11 DefaultTextures — BrunetonScattering is a Texture3D")
    {
        REQUIRE(CR().BrunetonScattering != nullptr);
        const nvrhi::TextureDesc& desc = CR().BrunetonScattering->getDesc();
        CHECK(desc.dimension == nvrhi::TextureDimension::Texture3D);
    }
}

// ============================================================================
// TEST SUITE: CommonResources — Dummy Buffers
// ============================================================================
TEST_SUITE("Graphics_DummyBuffers")
{
    // ------------------------------------------------------------------
    // TC-BUF-01: All dummy buffer handles are non-null
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-01 DummyBuffers — all handles are valid")
    {
        CHECK(CR().DummySRVByteAddressBuffer  != nullptr);
        CHECK(CR().DummyUAVByteAddressBuffer  != nullptr);
        CHECK(CR().DummySRVStructuredBuffer   != nullptr);
        CHECK(CR().DummyUAVStructuredBuffer   != nullptr);
        CHECK(CR().DummySRVTypedBuffer        != nullptr);
        CHECK(CR().DummyUAVTypedBuffer        != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-BUF-02: DummySRVByteAddressBuffer has canHaveRawViews
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-02 DummyBuffers — SRV ByteAddress has canHaveRawViews")
    {
        REQUIRE(CR().DummySRVByteAddressBuffer != nullptr);
        const nvrhi::BufferDesc& desc = CR().DummySRVByteAddressBuffer->getDesc();
        CHECK(desc.canHaveRawViews);
        CHECK_FALSE(desc.canHaveUAVs);
    }

    // ------------------------------------------------------------------
    // TC-BUF-03: DummyUAVByteAddressBuffer has canHaveRawViews and canHaveUAVs
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-03 DummyBuffers — UAV ByteAddress has canHaveRawViews and canHaveUAVs")
    {
        REQUIRE(CR().DummyUAVByteAddressBuffer != nullptr);
        const nvrhi::BufferDesc& desc = CR().DummyUAVByteAddressBuffer->getDesc();
        CHECK(desc.canHaveRawViews);
        CHECK(desc.canHaveUAVs);
    }

    // ------------------------------------------------------------------
    // TC-BUF-04: DummySRVStructuredBuffer has a non-zero structStride
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-04 DummyBuffers — SRV Structured has non-zero structStride")
    {
        REQUIRE(CR().DummySRVStructuredBuffer != nullptr);
        const nvrhi::BufferDesc& desc = CR().DummySRVStructuredBuffer->getDesc();
        CHECK(desc.structStride > 0u);
        CHECK_FALSE(desc.canHaveUAVs);
    }

    // ------------------------------------------------------------------
    // TC-BUF-05: DummyUAVStructuredBuffer has structStride and canHaveUAVs
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-05 DummyBuffers — UAV Structured has structStride and canHaveUAVs")
    {
        REQUIRE(CR().DummyUAVStructuredBuffer != nullptr);
        const nvrhi::BufferDesc& desc = CR().DummyUAVStructuredBuffer->getDesc();
        CHECK(desc.structStride > 0u);
        CHECK(desc.canHaveUAVs);
    }

    // ------------------------------------------------------------------
    // TC-BUF-06: DummySRVTypedBuffer has canHaveTypedViews and a valid format
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-06 DummyBuffers — SRV Typed has canHaveTypedViews and valid format")
    {
        REQUIRE(CR().DummySRVTypedBuffer != nullptr);
        const nvrhi::BufferDesc& desc = CR().DummySRVTypedBuffer->getDesc();
        CHECK(desc.canHaveTypedViews);
        CHECK(desc.format != nvrhi::Format::UNKNOWN);
        CHECK_FALSE(desc.canHaveUAVs);
    }

    // ------------------------------------------------------------------
    // TC-BUF-07: DummyUAVTypedBuffer has canHaveTypedViews, canHaveUAVs, valid format
    // ------------------------------------------------------------------
    TEST_CASE("TC-BUF-07 DummyBuffers — UAV Typed has canHaveTypedViews, canHaveUAVs, valid format")
    {
        REQUIRE(CR().DummyUAVTypedBuffer != nullptr);
        const nvrhi::BufferDesc& desc = CR().DummyUAVTypedBuffer->getDesc();
        CHECK(desc.canHaveTypedViews);
        CHECK(desc.canHaveUAVs);
        CHECK(desc.format != nvrhi::Format::UNKNOWN);
    }

}


