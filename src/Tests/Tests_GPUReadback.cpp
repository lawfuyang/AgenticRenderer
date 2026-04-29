// Tests_GPUReadback.cpp
//
// Systems under test: GPU texture creation, GPU buffer creation,
//                     staging texture readback, GPU sampler state.
//
// Prerequisites: g_Renderer fully initialized (RHI + CommonResources).
//
// Test coverage:
//   - CreateTestTexture2D 1x1 RGBA8_UNORM succeeds (non-null handle)
//   - CreateTestTexture2D 1x1 R32_FLOAT succeeds
//   - ReadbackTexelRGBA8 red/green/blue/black pixels: correct channel values
//   - ReadbackTexelFloat 1.0f → reads ~1.0f
//   - ReadbackTexelFloat 0.5f → reads ~0.5f
//   - Staging texture creation (nvrhi::StagingTextureHandle) is non-null
//   - Texture descriptor reports correct dimensions
//   - ReadbackTexelRGBA8 non-zero interior texel in a 4x4 texture
//   - GPU buffer creation: structured, constant, index, vertex, UAV, indirect args
//   - Buffer descriptor byte size matches requested
//   - SPD atomic counter descriptor byte size > 0
//   - InstanceDataBuffer capacity covers all CPU instances (regression)
//
// Run with: HobbyRenderer --run-tests=*GPUReadback*
// ============================================================================

#include "TestFixtures.h"

#include "Renderer.h"

namespace
{
    // Convenience: pack RGBA into a uint32
    static inline uint32_t MakeRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }

    // Unpack channels from ReadbackTexelRGBA8 result
    struct RGBA8 { uint8_t r, g, b, a; };
    static RGBA8 Unpack(uint32_t packed)
    {
        return { (uint8_t)(packed & 0xFF),
                 (uint8_t)((packed >> 8) & 0xFF),
                 (uint8_t)((packed >> 16) & 0xFF),
                 (uint8_t)((packed >> 24) & 0xFF) };
    }
} // anonymous namespace

// ============================================================================
// TEST SUITE: GPUReadback_TextureCreation
// ============================================================================
TEST_SUITE("GPUReadback_TextureCreation")
{
    // ------------------------------------------------------------------
    // TC-GRB-01: CreateTestTexture2D 1x1 RGBA8_UNORM returns non-null
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-01 TextureCreation - 1x1 RGBA8 texture is non-null")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t pixel = MakeRGBA8(128, 64, 32, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &pixel, sizeof(uint32_t), "TC-GRB-01");
        CHECK(tex != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-GRB-04: CreateTestTexture2D 1x1 R32_FLOAT returns non-null
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-04 TextureCreation - 1x1 R32_FLOAT texture is non-null")
    {
        REQUIRE(DEV() != nullptr);
        const float val = 0.75f;
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::R32_FLOAT,
                                                       &val, sizeof(float), "TC-GRB-04");
        CHECK(tex != nullptr);
    }

    // ------------------------------------------------------------------
    // TC-GRB-05: Texture descriptor reports correct dimensions
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-05 TextureCreation - texture descriptor reports correct dimensions")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t pixel = MakeRGBA8(255, 0, 0, 255);
        constexpr uint32_t W = 8, H = 4;
        std::vector<uint32_t> pixels(W * H, pixel);
        nvrhi::TextureHandle tex = CreateTestTexture2D(W, H, nvrhi::Format::RGBA8_UNORM,
                                                       pixels.data(), W * sizeof(uint32_t), "TC-GRB-05");
        REQUIRE(tex != nullptr);
        const auto& desc = tex->getDesc();
        CHECK(desc.width  == W);
        CHECK(desc.height == H);
    }

    // ------------------------------------------------------------------
    // TC-GRB-08: Staging texture creation from existing texture is non-null
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-08 TextureCreation - staging texture is non-null")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t pixel = MakeRGBA8(0, 0, 255, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &pixel, sizeof(uint32_t), "TC-GRB-08-src");
        REQUIRE(tex != nullptr);

        nvrhi::StagingTextureHandle staging = DEV()->createStagingTexture(tex->getDesc(),
                                                                           nvrhi::CpuAccessMode::Read);
        CHECK(staging != nullptr);
    }
}

// ============================================================================
// TEST SUITE: GPUReadback_TexelValues
// ============================================================================
TEST_SUITE("GPUReadback_TexelValues")
{
    // ------------------------------------------------------------------
    // TC-GRB-RED: ReadbackTexelRGBA8 — pure red pixel reads R=255,G=0,B=0,A=255
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-RED ReadbackTexel - red pixel RGBA values correct")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t srcPixel = MakeRGBA8(255, 0, 0, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &srcPixel, sizeof(uint32_t), "TC-RED");
        REQUIRE(tex != nullptr);

        const auto px = Unpack(ReadbackTexelRGBA8(tex, 0, 0));
        CHECK(px.r == 255);
        CHECK(px.g == 0);
        CHECK(px.b == 0);
        CHECK(px.a == 255);
    }

    // ------------------------------------------------------------------
    // TC-GRB-GRN: ReadbackTexelRGBA8 — pure green pixel
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-GRN ReadbackTexel - green pixel RGBA values correct")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t srcPixel = MakeRGBA8(0, 255, 0, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &srcPixel, sizeof(uint32_t), "TC-GRN");
        REQUIRE(tex != nullptr);

        const auto px = Unpack(ReadbackTexelRGBA8(tex, 0, 0));
        CHECK(px.r == 0);
        CHECK(px.g == 255);
        CHECK(px.b == 0);
        CHECK(px.a == 255);
    }

    // ------------------------------------------------------------------
    // TC-GRB-BLU: ReadbackTexelRGBA8 — pure blue pixel
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-BLU ReadbackTexel - blue pixel RGBA values correct")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t srcPixel = MakeRGBA8(0, 0, 255, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &srcPixel, sizeof(uint32_t), "TC-BLU");
        REQUIRE(tex != nullptr);

        const auto px = Unpack(ReadbackTexelRGBA8(tex, 0, 0));
        CHECK(px.r == 0);
        CHECK(px.g == 0);
        CHECK(px.b == 255);
        CHECK(px.a == 255);
    }

    // ------------------------------------------------------------------
    // TC-GRB-BLK: ReadbackTexelRGBA8 — black pixel
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-BLK ReadbackTexel - black pixel all channels are 0")
    {
        REQUIRE(DEV() != nullptr);
        const uint32_t srcPixel = MakeRGBA8(0, 0, 0, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::RGBA8_UNORM,
                                                       &srcPixel, sizeof(uint32_t), "TC-BLK");
        REQUIRE(tex != nullptr);

        const auto px = Unpack(ReadbackTexelRGBA8(tex, 0, 0));
        CHECK(px.r == 0);
        CHECK(px.g == 0);
        CHECK(px.b == 0);
        // Alpha may be 255 depending on upload path — just check RGB are 0.
    }

    // ------------------------------------------------------------------
    // TC-GRB-F1: ReadbackTexelFloat 1.0f → reads ~1.0f
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-F1 ReadbackTexel - R32_FLOAT 1.0f reads back as 1.0f")
    {
        REQUIRE(DEV() != nullptr);
        const float val = 1.0f;
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::R32_FLOAT,
                                                       &val, sizeof(float), "TC-F1");
        REQUIRE(tex != nullptr);

        const float readback = ReadbackTexelFloat(tex, 0, 0);
        CHECK(readback == doctest::Approx(1.0f).epsilon(0.001f));
    }

    // ------------------------------------------------------------------
    // TC-GRB-F05: ReadbackTexelFloat 0.5f → reads ~0.5f
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-F05 ReadbackTexel - R32_FLOAT 0.5f reads back as ~0.5f")
    {
        REQUIRE(DEV() != nullptr);
        const float val = 0.5f;
        nvrhi::TextureHandle tex = CreateTestTexture2D(1, 1, nvrhi::Format::R32_FLOAT,
                                                       &val, sizeof(float), "TC-F05");
        REQUIRE(tex != nullptr);

        const float readback = ReadbackTexelFloat(tex, 0, 0);
        CHECK(readback == doctest::Approx(0.5f).epsilon(0.001f));
    }

    // ------------------------------------------------------------------
    // TC-GRB-F4T: ReadbackTexelRGBA8 — non-zero interior texel in a 4x4 texture
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-F4T ReadbackTexel - 4x4 texture correct interior texel")
    {
        REQUIRE(DEV() != nullptr);
        std::vector<uint32_t> pixels(4 * 4, MakeRGBA8(0, 0, 0, 255));
        // Set texel (2, 1) = red
        pixels[1 * 4 + 2] = MakeRGBA8(255, 0, 0, 255);
        nvrhi::TextureHandle tex = CreateTestTexture2D(4, 4, nvrhi::Format::RGBA8_UNORM,
                                                       pixels.data(), 4 * sizeof(uint32_t), "TC-F4T");
        REQUIRE(tex != nullptr);

        const auto px = Unpack(ReadbackTexelRGBA8(tex, 2, 1));
        CHECK(px.r == 255);
        CHECK(px.g == 0);
        CHECK(px.b == 0);
    }
}

// ============================================================================
// TEST SUITE: GPUReadback_BufferCreation
// ============================================================================
TEST_SUITE("GPUReadback_BufferCreation")
{
    // ------------------------------------------------------------------
    // TC-GRB-BUF-01: GPU buffer creation — structured, constant, index, vertex, UAV, indirect args
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-BUF-01 BufferCreation - all common buffer types are non-null")
    {
        REQUIRE(DEV() != nullptr);

        // Structured
        {
            nvrhi::BufferDesc desc;
            desc.byteSize     = 64;
            desc.structStride = sizeof(float);
            desc.debugName    = "TC-BUF-Structured";
            CHECK(DEV()->createBuffer(desc) != nullptr);
        }
        // Constant (D3D12 minimum CBV alignment)
        {
            nvrhi::BufferDesc desc;
            desc.byteSize         = 256;
            desc.isConstantBuffer = true;
            desc.debugName        = "TC-BUF-Constant";
            CHECK(DEV()->createBuffer(desc) != nullptr);
        }
        // Index (32-bit, 3 indices)
        {
            nvrhi::BufferDesc desc;
            desc.byteSize      = 12;
            desc.isIndexBuffer = true;
            desc.debugName     = "TC-BUF-Index";
            CHECK(DEV()->createBuffer(desc) != nullptr);
        }
        // Vertex
        {
            nvrhi::BufferDesc desc;
            desc.byteSize       = sizeof(float) * 3 * 3;
            desc.isVertexBuffer = true;
            desc.debugName      = "TC-BUF-Vertex";
            CHECK(DEV()->createBuffer(desc) != nullptr);
        }
        // UAV
        {
            nvrhi::BufferDesc desc;
            desc.byteSize    = 64;
            desc.canHaveUAVs = true;
            desc.debugName   = "TC-BUF-UAV";
            CHECK(DEV()->createBuffer(desc) != nullptr);
        }
        // Indirect args
        {
            nvrhi::BufferDesc desc;
            desc.byteSize          = sizeof(uint32_t) * 5;
            desc.isDrawIndirectArgs = true;
            desc.canHaveUAVs       = true;
            desc.debugName         = "TC-BUF-IndirectArgs";
            CHECK(DEV()->createBuffer(desc) != nullptr);
        }
    }

    // ------------------------------------------------------------------
    // TC-GRB-BUF-05: Buffer descriptor byte size matches requested
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-BUF-05 BufferCreation - buffer byteSize matches request")
    {
        REQUIRE(DEV() != nullptr);

        constexpr uint64_t reqSize = 512;
        nvrhi::BufferDesc desc;
        desc.byteSize  = reqSize;
        desc.debugName = "TC-BUF-SizeCheck";

        auto buf = DEV()->createBuffer(desc);
        REQUIRE(buf != nullptr);
        CHECK(buf->getDesc().byteSize >= reqSize);
    }

    // ------------------------------------------------------------------
    // TC-GRB-BUF-08: SPD atomic counter descriptor byte size > 0
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-BUF-08 BufferCreation - SPD atomic counter desc byteSize > 0")
    {
        REQUIRE(DEV() != nullptr);

        const RGBufferDesc spdDesc = RenderGraph::GetSPDAtomicCounterDesc("TC-SPD");
        CHECK(spdDesc.m_NvrhiDesc.byteSize > 0);
        CHECK(spdDesc.m_NvrhiDesc.canHaveUAVs);
    }

    // ------------------------------------------------------------------
    // TC-GRB-BUF-10: m_InstanceDataBuffer capacity covers all CPU instances
    // Regression: an off-by-one in the dirty-range upper bound caused
    // UploadDirtyInstanceTransforms() to attempt writing
    //   (size) * sizeof(PerInstanceData)  bytes
    // into a buffer that was only sized for
    //   (size) * sizeof(PerInstanceData)  bytes starting at offset 0,
    // but with second = size (out-of-bounds) the write region exceeded the
    // buffer end and NVRHI fired a validation error.
    // This test ensures the GPU buffer is always large enough to hold every
    // CPU-side instance, and that a full-range dirty upload does not overflow.
    // ------------------------------------------------------------------
    TEST_CASE_FIXTURE(MinimalSceneFixture, "TC-GRB-BUF-10 BufferCreation - InstanceDataBuffer capacity covers all CPU instances")
    {
        REQUIRE(DEV() != nullptr);

        const size_t instanceCount = g_Renderer.m_Scene.m_InstanceData.size();
        REQUIRE(instanceCount > 0);

        // GPU buffer must exist and be large enough for all instances.
        REQUIRE(g_Renderer.m_Scene.m_InstanceDataBuffer != nullptr);
        const uint64_t bufferBytes = g_Renderer.m_Scene.m_InstanceDataBuffer->getDesc().byteSize;
        const uint64_t requiredBytes = instanceCount * sizeof(srrhi::PerInstanceData);
        INFO("instanceCount=" << instanceCount
             << " requiredBytes=" << requiredBytes
             << " bufferBytes=" << bufferBytes);
        CHECK(bufferBytes >= requiredBytes);

        // A full-range dirty upload (all valid indices) must not crash or fire
        // a validation error.  Use the last valid index as the upper bound.
        const uint32_t lastIdx = static_cast<uint32_t>(instanceCount) - 1u;
        g_Renderer.m_Scene.m_InstanceDirtyRange = { 0u, lastIdx };
        REQUIRE(g_Renderer.m_Scene.AreInstanceTransformsDirty());
        CHECK_NOTHROW(g_Renderer.UploadDirtyInstanceTransforms());
        CHECK(!g_Renderer.m_Scene.AreInstanceTransformsDirty());
    }
}
