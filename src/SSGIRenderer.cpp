#include "Renderer.h"
#include "CommonResources.h"

#include "shaders/srrhi/cpp/SSGI.h"
#include "shaders/srrhi/cpp/SSGITemporalReproject.h"
#include "shaders/srrhi/cpp/SSGIDenoise.h"
#include "shaders/srrhi/cpp/SSGICompose.h"

// ---------------------------------------------------------------------------
// Render Graph handles
// ---------------------------------------------------------------------------
extern RGTextureHandle g_RG_DepthTexture;
extern RGTextureHandle g_RG_GBufferAlbedo;
extern RGTextureHandle g_RG_GBufferNormals;
extern RGTextureHandle g_RG_GBufferORM;
extern RGTextureHandle g_RG_GBufferEmissive;
extern RGTextureHandle g_RG_GBufferMotionVectors;
extern RGTextureHandle g_RG_ShadowMask;

RGTextureHandle        g_RG_SSGIComposed;   // RGBA16_FLOAT — final indirect GI term, consumed by DeferredRenderer

// ---------------------------------------------------------------------------
// SSGIRenderer — screen-space global illumination (NormalBasic mode only)
// 4 fullscreen passes: ray march → temporal reproject → Poisson denoise → compose.
// The denoise pass writes into the accum ping-pong write slot, so the denoised
// result becomes next frame's reprojection/feedback source
// ---------------------------------------------------------------------------
class SSGIRenderer : public IRenderer
{
public:
    bool Setup(RenderGraph& renderGraph) override
    {
        if (g_Renderer.m_Mode != RenderingMode::NormalBasic ||
            g_Renderer.m_IndirectLightingTechnique != srrhi::IndirectLightingMode::INDIRECT_LIGHTING_MODE_SSGI)
            return false;

        auto [width, height] = g_Renderer.SwapchainSize();

        auto makeColorDesc = [&](const char* name)
        {
            RGTextureDesc desc;
            desc.m_NvrhiDesc.width          = width;
            desc.m_NvrhiDesc.height         = height;
            desc.m_NvrhiDesc.format         = nvrhi::Format::RGBA16_FLOAT;
            desc.m_NvrhiDesc.isRenderTarget = true;
            desc.m_NvrhiDesc.debugName      = name;
            desc.m_NvrhiDesc.initialState   = nvrhi::ResourceStates::RenderTarget;
            desc.m_NvrhiDesc.setClearValue(nvrhi::Color{ 0.0f });
            desc.m_NvrhiDesc.keepInitialState = true;
            return desc;
        };

        // Persistent ping-pong accumulation (rgb = denoised GI, a = age)
        bool newAccum = false;
        newAccum |= renderGraph.DeclarePersistentTexture(makeColorDesc("SSGIAccumDiffuse_0"),  m_AccumDiffuse[0]);
        newAccum |= renderGraph.DeclarePersistentTexture(makeColorDesc("SSGIAccumDiffuse_1"),  m_AccumDiffuse[1]);
        newAccum |= renderGraph.DeclarePersistentTexture(makeColorDesc("SSGIAccumSpecular_0"), m_AccumSpecular[0]);
        newAccum |= renderGraph.DeclarePersistentTexture(makeColorDesc("SSGIAccumSpecular_1"), m_AccumSpecular[1]);
        if (newAccum)
            m_bClearAccum = true;

        renderGraph.DeclareTexture(makeColorDesc("SSGIRawDiffuse"),       m_RawDiffuse);
        renderGraph.DeclareTexture(makeColorDesc("SSGIRawSpecular"),      m_RawSpecular);
        renderGraph.DeclareTexture(makeColorDesc("SSGITemporalDiffuse"),  m_TemporalDiffuse);
        renderGraph.DeclareTexture(makeColorDesc("SSGITemporalSpecular"), m_TemporalSpecular);
        renderGraph.DeclareTexture(makeColorDesc("SSGIDenoiseScratchDiffuse"),  m_DenoiseScratchDiffuse);
        renderGraph.DeclareTexture(makeColorDesc("SSGIDenoiseScratchSpecular"), m_DenoiseScratchSpecular);
        renderGraph.DeclareTexture(makeColorDesc("SSGIComposed"),         g_RG_SSGIComposed);

        renderGraph.ReadTexture(g_RG_DepthTexture);
        renderGraph.ReadTexture(g_RG_GBufferAlbedo);
        renderGraph.ReadTexture(g_RG_GBufferNormals);
        renderGraph.ReadTexture(g_RG_GBufferORM);
        renderGraph.ReadTexture(g_RG_GBufferEmissive);
        renderGraph.ReadTexture(g_RG_GBufferMotionVectors);
        renderGraph.ReadTexture(g_RG_ShadowMask);
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        PROFILE_FUNCTION();

        nvrhi::DeviceHandle device = g_Renderer.m_RHI->m_NvrhiDevice;

        const uint32_t readIdx  = m_PingPong;
        const uint32_t writeIdx = 1 - m_PingPong;

        nvrhi::TextureHandle depth         = renderGraph.GetTexture(g_RG_DepthTexture,         RGResourceAccessMode::Read);
        nvrhi::TextureHandle gbufferAlbedo = renderGraph.GetTexture(g_RG_GBufferAlbedo,        RGResourceAccessMode::Read);
        nvrhi::TextureHandle gbufferNormals = renderGraph.GetTexture(g_RG_GBufferNormals,      RGResourceAccessMode::Read);
        nvrhi::TextureHandle gbufferORM    = renderGraph.GetTexture(g_RG_GBufferORM,           RGResourceAccessMode::Read);
        nvrhi::TextureHandle gbufferEmissive = renderGraph.GetTexture(g_RG_GBufferEmissive,    RGResourceAccessMode::Read);
        nvrhi::TextureHandle motionVectors = renderGraph.GetTexture(g_RG_GBufferMotionVectors, RGResourceAccessMode::Read);
        nvrhi::TextureHandle shadowMask    = renderGraph.GetTexture(g_RG_ShadowMask,           RGResourceAccessMode::Read);

        nvrhi::TextureHandle accumReadDiffuse   = renderGraph.GetTexture(m_AccumDiffuse[readIdx],   RGResourceAccessMode::Read);
        nvrhi::TextureHandle accumReadSpecular  = renderGraph.GetTexture(m_AccumSpecular[readIdx],  RGResourceAccessMode::Read);
        nvrhi::TextureHandle accumWriteDiffuse  = renderGraph.GetTexture(m_AccumDiffuse[writeIdx],  RGResourceAccessMode::Write);
        nvrhi::TextureHandle accumWriteSpecular = renderGraph.GetTexture(m_AccumSpecular[writeIdx], RGResourceAccessMode::Write);

        nvrhi::TextureHandle rawDiffuse       = renderGraph.GetTexture(m_RawDiffuse,       RGResourceAccessMode::Write);
        nvrhi::TextureHandle rawSpecular      = renderGraph.GetTexture(m_RawSpecular,      RGResourceAccessMode::Write);
        nvrhi::TextureHandle temporalDiffuse  = renderGraph.GetTexture(m_TemporalDiffuse,  RGResourceAccessMode::Write);
        nvrhi::TextureHandle temporalSpecular = renderGraph.GetTexture(m_TemporalSpecular, RGResourceAccessMode::Write);
        nvrhi::TextureHandle scratchDiffuse   = renderGraph.GetTexture(m_DenoiseScratchDiffuse,  RGResourceAccessMode::Write);
        nvrhi::TextureHandle scratchSpecular  = renderGraph.GetTexture(m_DenoiseScratchSpecular, RGResourceAccessMode::Write);
        nvrhi::TextureHandle composed         = renderGraph.GetTexture(g_RG_SSGIComposed,  RGResourceAccessMode::Write);

        // Freshly (re)allocated accum history has undefined content — clear it so the
        // temporal pass starts at age 0 instead of blending against garbage.
        if (m_bClearAccum)
        {
            commandList->clearTextureFloat(accumWriteDiffuse,  nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
            commandList->clearTextureFloat(accumWriteSpecular, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
            commandList->clearTextureFloat(accumReadDiffuse,   nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
            commandList->clearTextureFloat(accumReadSpecular,  nvrhi::AllSubresources, nvrhi::Color{ 0.0f });
            m_bClearAccum = false;
        }

        const Vector3 camPos = g_Renderer.m_Scene.m_Camera.GetPosition();
        const float sunIntensity = g_Renderer.m_EnableSky ? g_Renderer.m_Scene.GetSunIntensity() : 0.0f;

        // ── Pass 1: ray march → RawDiffuse/RawSpecular ─────────────────────
        {
            const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(srrhi::SSGIConstants), "SSGICB", 1);
            const nvrhi::BufferHandle cb = device->createBuffer(cbDesc);

            srrhi::SSGIConstants constants;
            constants.SetView(g_Renderer.m_Scene.m_View);
            constants.SetSunDirection(g_Renderer.m_Scene.GetSunDirection());
            constants.SetSunIntensity(sunIntensity);
            constants.SetFrame(g_Renderer.m_FrameNumber);
            constants.SetRayDistance(g_Renderer.m_SSGI_RayDistance);
            constants.SetThickness(g_Renderer.m_SSGI_Thickness);
            constants.SetSteps(g_Renderer.m_SSGI_Steps);
            constants.SetRefineSteps(g_Renderer.m_SSGI_RefineSteps);
            commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

            srrhi::SSGIInputs inputs;
            inputs.SetSSGICB(cb);
            inputs.SetGBufferAlbedo(gbufferAlbedo);
            inputs.SetGBufferNormals(gbufferNormals);
            inputs.SetGBufferORM(gbufferORM);
            inputs.SetGBufferEmissive(gbufferEmissive);
            inputs.SetShadowMask(shadowMask);
            inputs.SetDepth(depth);
            inputs.SetMotionVectors(motionVectors);
            inputs.SetAccumDiffuse(accumReadDiffuse);
            inputs.SetAccumSpecular(accumReadSpecular);
            inputs.SetBlueNoise(CommonResources::GetInstance().BlueNoiseTexture);
            inputs.SetLinearSampler(CommonResources::GetInstance().LinearClamp);
            inputs.SetPointSampler(CommonResources::GetInstance().PointClamp);

            nvrhi::BindingSetDesc bset = Renderer::CreateBindingSetDesc(inputs);

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(rawDiffuse);
            fbDesc.addColorAttachment(rawSpecular);
            nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(fbDesc);

            Renderer::RenderPassParams params;
            params.commandList    = commandList;
            params.shaderID       = ShaderID::SSGI_SSGI_PSMAIN;
            params.bindingSetDesc = bset;
            params.framebuffer    = framebuffer;
            g_Renderer.AddFullScreenPass(params);
        }

        // With denoising disabled the temporal pass renders straight into the accum write slot
        // (the denoiser's output target), so the denoise passes can simply be skipped. Avoids
        // both a redundant full-res copy and the D3D12 requirement that a placed render target
        // be initialized by Discard/Clear/Copy before it can be used as a copy source.
        const bool bDenoise = g_Renderer.m_SSGI_bDenoiseEnabled;
        nvrhi::TextureHandle temporalOutDiffuse  = bDenoise ? temporalDiffuse  : accumWriteDiffuse;
        nvrhi::TextureHandle temporalOutSpecular = bDenoise ? temporalSpecular : accumWriteSpecular;

        // ── Pass 2: temporal reproject → TemporalDiffuse/TemporalSpecular ──
        {
            const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(srrhi::SSGITemporalConstants), "SSGITemporalCB", 1);
            const nvrhi::BufferHandle cb = device->createBuffer(cbDesc);

            srrhi::SSGITemporalConstants constants;
            constants.SetView(g_Renderer.m_Scene.m_View);
            constants.SetViewPrev(g_Renderer.m_Scene.m_ViewPrev);
            constants.SetBlend(g_Renderer.m_SSGI_TemporalBlend);
            commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

            srrhi::SSGITemporalReprojectInputs inputs;
            inputs.SetTemporalCB(cb);
            inputs.SetRawDiffuse(rawDiffuse);
            inputs.SetRawSpecular(rawSpecular);
            inputs.SetPrevAccumDiffuse(accumReadDiffuse);
            inputs.SetPrevAccumSpecular(accumReadSpecular);
            inputs.SetMotionVectors(motionVectors);
            inputs.SetDepth(depth);
            inputs.SetGBufferNormals(gbufferNormals);
            inputs.SetGBufferORM(gbufferORM);
            inputs.SetLinearSampler(CommonResources::GetInstance().LinearClamp);
            inputs.SetPointSampler(CommonResources::GetInstance().PointClamp);

            nvrhi::BindingSetDesc bset = Renderer::CreateBindingSetDesc(inputs);

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(temporalOutDiffuse);
            fbDesc.addColorAttachment(temporalOutSpecular);
            nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(fbDesc);

            Renderer::RenderPassParams params;
            params.commandList    = commandList;
            params.shaderID       = ShaderID::SSGITEMPORALREPROJECT_SSGITEMPORAL_PSMAIN;
            params.bindingSetDesc = bset;
            params.framebuffer    = framebuffer;
            g_Renderer.AddFullScreenPass(params);
        }

        // ── Pass 3: Poisson denoise, iterated → Accum[write] (denoised history) ──
        // A single 8-tap pass is nowhere near enough for a ~1 spp estimate; the reference runs
        // 2 * iterations of it. Each iteration doubles the kernel radius (a-trous style) so the
        // footprint grows geometrically instead of just being resampled at the same scale.
        // Ping-pongs between the scratch pair and the accum write slot, choosing the starting
        // target by parity so the final iteration always lands in the accum write slot.
        if (bDenoise)
        {
            const uint32_t iterations = static_cast<uint32_t>(std::max(1, g_Renderer.m_SSGI_DenoiseIterations));

            for (uint32_t i = 0; i < iterations; i++)
            {
                const bool bWriteToAccum = (((iterations - 1u - i) & 1u) == 0u);

                nvrhi::TextureHandle dstDiffuse  = bWriteToAccum ? accumWriteDiffuse  : scratchDiffuse;
                nvrhi::TextureHandle dstSpecular = bWriteToAccum ? accumWriteSpecular : scratchSpecular;
                nvrhi::TextureHandle srcDiffuse  = (i == 0) ? temporalOutDiffuse  : (bWriteToAccum ? scratchDiffuse  : accumWriteDiffuse);
                nvrhi::TextureHandle srcSpecular = (i == 0) ? temporalOutSpecular : (bWriteToAccum ? scratchSpecular : accumWriteSpecular);

                const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(srrhi::SSGIDenoiseConstants), "SSGIDenoiseCB", 1);
                const nvrhi::BufferHandle cb = device->createBuffer(cbDesc);

                srrhi::SSGIDenoiseConstants constants;
                constants.SetView(g_Renderer.m_Scene.m_View);
                // Offset the seed per iteration so the poisson disk rotation differs between them
                constants.SetFrame(g_Renderer.m_FrameNumber * iterations + i);
                constants.SetRadius(g_Renderer.m_SSGI_DenoiseRadius * static_cast<float>(1u << i));
                constants.SetPhi(g_Renderer.m_SSGI_DenoisePhi);
                constants.SetLumaPhi(g_Renderer.m_SSGI_DenoiseLumaPhi);
                constants.SetDepthPhi(g_Renderer.m_SSGI_DenoiseDepthPhi);
                constants.SetNormalPhi(g_Renderer.m_SSGI_DenoiseNormalPhi);
                constants.SetRoughnessPhi(g_Renderer.m_SSGI_DenoiseRoughnessPhi);
                constants.SetSpecularPhi(g_Renderer.m_SSGI_DenoiseSpecularPhi);
                commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

                srrhi::SSGIDenoiseInputs inputs;
                inputs.SetDenoiseCB(cb);
                inputs.SetInputDiffuse(srcDiffuse);
                inputs.SetInputSpecular(srcSpecular);
                inputs.SetDepth(depth);
                inputs.SetGBufferNormals(gbufferNormals);
                inputs.SetGBufferORM(gbufferORM);
                inputs.SetBlueNoise(CommonResources::GetInstance().BlueNoiseTexture);
                inputs.SetPointSampler(CommonResources::GetInstance().PointClamp);

                nvrhi::BindingSetDesc bset = Renderer::CreateBindingSetDesc(inputs);

                nvrhi::FramebufferDesc fbDesc;
                fbDesc.addColorAttachment(dstDiffuse);
                fbDesc.addColorAttachment(dstSpecular);
                nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(fbDesc);

                Renderer::RenderPassParams params;
                params.commandList    = commandList;
                params.shaderID       = ShaderID::SSGIDENOISE_SSGIDENOISE_PSMAIN;
                params.bindingSetDesc = bset;
                params.framebuffer    = framebuffer;
                g_Renderer.AddFullScreenPass(params);
            }
        }

        // ── Pass 4: Fresnel compose → SSGIComposed ─────────────────────────
        {
            const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(srrhi::SSGIComposeConstants), "SSGIComposeCB", 1);
            const nvrhi::BufferHandle cb = device->createBuffer(cbDesc);

            srrhi::SSGIComposeConstants constants;
            constants.SetView(g_Renderer.m_Scene.m_View);
            constants.SetSunDirection(g_Renderer.m_Scene.GetSunDirection());
            constants.SetSunIntensity(sunIntensity);
            constants.SetDebugMode(g_Renderer.m_SSGI_DebugMode);
            commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

            srrhi::SSGIComposeInputs inputs;
            inputs.SetComposeCB(cb);
            inputs.SetDenoisedDiffuse(accumWriteDiffuse);
            inputs.SetDenoisedSpecular(accumWriteSpecular);
            inputs.SetGBufferAlbedo(gbufferAlbedo);
            inputs.SetGBufferNormals(gbufferNormals);
            inputs.SetGBufferORM(gbufferORM);
            inputs.SetGBufferEmissive(gbufferEmissive);
            inputs.SetShadowMask(shadowMask);
            inputs.SetDepth(depth);
            inputs.SetRawDiffuse(rawDiffuse);
            inputs.SetRawSpecular(rawSpecular);
            inputs.SetTemporalDiffuse(temporalOutDiffuse);
            inputs.SetTemporalSpecular(temporalOutSpecular);
            inputs.SetPointSampler(CommonResources::GetInstance().PointClamp);

            nvrhi::BindingSetDesc bset = Renderer::CreateBindingSetDesc(inputs);

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(composed);
            nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(fbDesc);

            Renderer::RenderPassParams params;
            params.commandList    = commandList;
            params.shaderID       = ShaderID::SSGICOMPOSE_SSGICOMPOSE_PSMAIN;
            params.bindingSetDesc = bset;
            params.framebuffer    = framebuffer;
            g_Renderer.AddFullScreenPass(params);
        }

        // flip ping-pong: the slot just written by the denoise pass becomes next
        // frame's reprojection/feedback read source
        m_PingPong = writeIdx;
    }

    const char* GetName() const override { return "SSGIRenderer"; }

private:
    RGTextureHandle m_AccumDiffuse[2];
    RGTextureHandle m_AccumSpecular[2];
    uint32_t        m_PingPong = 0;   // read slot index (write slot = 1 - m_PingPong)
    bool            m_bClearAccum = false;

    RGTextureHandle m_RawDiffuse;
    RGTextureHandle m_RawSpecular;
    RGTextureHandle m_TemporalDiffuse;
    RGTextureHandle m_TemporalSpecular;
    RGTextureHandle m_DenoiseScratchDiffuse;
    RGTextureHandle m_DenoiseScratchSpecular;
};

REGISTER_RENDERER(SSGIRenderer);
