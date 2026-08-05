#include "Renderer.h"
#include "CommonResources.h"

#include "shaders/srrhi/cpp/ShadowMask.h"
#include "shaders/srrhi/cpp/ScreenSpaceShadows.h"
#include "../external/bend_sss_cpu.h"

// ---------------------------------------------------------------------------
// Render Graph handles — g_RG_CSMShadowMap defined in ShadowRenderer.cpp
// ---------------------------------------------------------------------------
extern RGTextureHandle g_RG_CSMShadowMap;
extern RGTextureHandle g_RG_DepthTexture;
extern RGTextureHandle g_RG_GBufferNormals;
RGTextureHandle        g_RG_ShadowMask;

// ---------------------------------------------------------------------------
// ShadowMaskRenderer — fullscreen compute: CSM evaluation -> R8_UNORM mask
// ---------------------------------------------------------------------------
class ShadowMaskRenderer : public IRenderer
{
public:
    bool Setup(RenderGraph& renderGraph) override
    {
        if (g_Renderer.m_Mode != RenderingMode::NormalBasic)
            return false;

        auto [width, height] = g_Renderer.SwapchainSize();

        // Declare the R8_UNORM shadow mask (transient, written each frame)
        RGTextureDesc maskDesc;
        maskDesc.m_NvrhiDesc.width            = width;
        maskDesc.m_NvrhiDesc.height           = height;
        maskDesc.m_NvrhiDesc.format           = nvrhi::Format::R8_UNORM;
        maskDesc.m_NvrhiDesc.isUAV            = true;
        maskDesc.m_NvrhiDesc.debugName        = "ShadowMask_RG";
        maskDesc.m_NvrhiDesc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
        maskDesc.m_NvrhiDesc.keepInitialState = true;
        renderGraph.DeclareTexture(maskDesc, g_RG_ShadowMask);

        // If CSM shadows are disabled, only the clear in Render() is needed
        if (!g_Renderer.m_EnableCSMShadows)
            return true;

        renderGraph.ReadTexture(g_RG_CSMShadowMap);
        renderGraph.ReadTexture(g_RG_DepthTexture);
        renderGraph.ReadTexture(g_RG_GBufferNormals);

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::DeviceHandle device = g_Renderer.m_RHI->m_NvrhiDevice;

        nvrhi::TextureHandle shadowMask = renderGraph.GetTexture(g_RG_ShadowMask, RGResourceAccessMode::Write);

        // If CSM shadows are disabled, clear to white (fully lit) and return
        if (!g_Renderer.m_EnableCSMShadows)
        {
            commandList->clearTextureFloat(shadowMask, nvrhi::AllSubresources, nvrhi::Color{ 1.0f });
            return;
        }

        auto [width, height] = g_Renderer.SwapchainSize();

        // -----------------------------------------------------------------------
        // Build constant buffer
        // -----------------------------------------------------------------------
        const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
            sizeof(srrhi::ShadowMaskConstants), "ShadowMaskCB", 1);
        const nvrhi::BufferHandle shadowMaskCB = device->createBuffer(cbDesc);

        srrhi::ShadowMaskConstants cb;

        Matrix allVPs[4];
        for (uint32_t i = 0; i < 4; i++)
            allVPs[i] = g_Renderer.m_CSMCascades[i].m_ViewProj;
        cb.SetShadowViewProj(allVPs);

        cb.SetClipToWorld(g_Renderer.m_Scene.m_View.m_MatClipToWorld);
        cb.SetWorldToView(g_Renderer.m_Scene.m_View.m_MatWorldToView);
        cb.SetCascadeSplits(Vector4{
            g_Renderer.m_CSMCascadeSplits[1],
            g_Renderer.m_CSMCascadeSplits[2],
            g_Renderer.m_CSMCascadeSplits[3],
            g_Renderer.m_CSMCascadeSplits[4]
        });
        cb.SetOutputSize(Vector2{ (float)width, (float)height });
        cb.SetConstantDepthBias(g_Renderer.m_CSMConstantDepthBias);

        // Per-cascade anisotropic normal bias
        // Pre-compute normalBias = userBias * texelSizeWorldSpace per cascade.
        // texelSize.x = 2 / (resolution * length(VP_row0_xyz))
        // texelSize.y = 2 / (resolution * length(VP_row1_xyz))
        // The shader then does: offset = abs(dot(row0, n) * bias.x) + abs(dot(row1, n) * bias.y)
        {
            const float res = (float)srrhi::CommonConsts::kShadowMapResolution;
            const float userBias = g_Renderer.m_CSMNormalBias;
            Vector4 packed[2] = {};
            for (uint32_t i = 0; i < 4; i++)
            {
                const Matrix& vp = g_Renderer.m_CSMCascades[i].m_ViewProj;
                // Row 0 xyz = light X basis in world space (row-major)
                float lx = std::sqrt(vp._11 * vp._11 + vp._12 * vp._12 + vp._13 * vp._13);
                // Row 1 xyz = light Y basis in world space (row-major)
                float ly = std::sqrt(vp._21 * vp._21 + vp._22 * vp._22 + vp._23 * vp._23);
                float texelSizeX = 2.0f / (res * std::max(lx, 1e-10f));
                float texelSizeY = 2.0f / (res * std::max(ly, 1e-10f));
                float biasX = userBias * texelSizeX;
                float biasY = userBias * texelSizeY;
                // Pack: [0].xy = cascade0, [0].zw = cascade1, [1].xy = cascade2, [1].zw = cascade3
                if (i & 1) { packed[i >> 1].z = biasX; packed[i >> 1].w = biasY; }
                else       { packed[i >> 1].x = biasX; packed[i >> 1].y = biasY; }
            }
            cb.SetNormalBias(packed);
        }
        cb.SetCSMDebugMode(g_Renderer.m_CSMDebugMode);

        cb.SetFrameIndex(g_Renderer.m_FrameNumber);

        commandList->writeBuffer(shadowMaskCB, &cb, sizeof(cb), 0);

        const CommonResources& cr = CommonResources::GetInstance();

        // -----------------------------------------------------------------------
        // Resolve textures
        // -----------------------------------------------------------------------
        nvrhi::TextureHandle depth     = renderGraph.GetTexture(g_RG_DepthTexture,   RGResourceAccessMode::Read);
        nvrhi::TextureHandle normals   = renderGraph.GetTexture(g_RG_GBufferNormals, RGResourceAccessMode::Read);
        nvrhi::TextureHandle shadowMap = renderGraph.GetTexture(g_RG_CSMShadowMap,   RGResourceAccessMode::Read);

        // -----------------------------------------------------------------------
        // Dispatch
        // -----------------------------------------------------------------------
        {
            srrhi::ShadowMaskInputs inputs;
            inputs.SetCB(shadowMaskCB);
            inputs.SetDepth(depth);
            inputs.SetGBufferNormals(normals);
            inputs.SetCSMShadowMap(shadowMap);
            inputs.SetRWShadowMask(shadowMask, 0);
            inputs.SetShadowSampler(cr.ShadowComparison);

            uint32_t shaderID = ShaderID::SHADOWMASK_SHADOWMASK_CSMAIN;

            Renderer::RenderPassParams params;
            params.commandList    = commandList;
            params.shaderID       = shaderID;
            params.bindingSetDesc = Renderer::CreateBindingSetDesc(inputs);
            params.dispatchParams = {
                .x = DivideAndRoundUp(width,  8u),
                .y = DivideAndRoundUp(height, 8u),
                .z = 1u
            };
            g_Renderer.AddComputePass(params);
        }

        // -------------------------------------------------------------------
        // Screen-Space Shadows pass
        // -------------------------------------------------------------------
        if (g_Renderer.m_EnableScreenSpaceShadows)
        {
            // Compute light projection: float4(sunDir, 0) * ViewProjectionMatrix
            // GetSunDirection() returns direction toward the sun.
            const Vector3 sunDir = g_Renderer.m_Scene.GetSunDirection();
            const Matrix& viewProj = g_Renderer.m_Scene.m_View.m_MatWorldToClipNoOffset;

            // Manual float4(sunDir, 0) * row-major VP matrix
            float lightProj[4];
            lightProj[0] = sunDir.x * viewProj._11 + sunDir.y * viewProj._21 + sunDir.z * viewProj._31;
            lightProj[1] = sunDir.x * viewProj._12 + sunDir.y * viewProj._22 + sunDir.z * viewProj._32;
            lightProj[2] = sunDir.x * viewProj._13 + sunDir.y * viewProj._23 + sunDir.z * viewProj._33;
            lightProj[3] = sunDir.x * viewProj._14 + sunDir.y * viewProj._24 + sunDir.z * viewProj._34;

            int viewportSize[2] = { (int)width, (int)height };
            int minBounds[2]    = { 0, 0 };
            int maxBounds[2]    = { (int)width, (int)height };

            // Reversed-Z: near=1, far=0. Non-expanded Z range.
            Bend::DispatchList dispatchList = Bend::BuildDispatchList(
                lightProj, viewportSize, minBounds, maxBounds, /*inExpandedZRange=*/false, /*inWaveSize=*/64);

            // Pack flags
            uint32_t flags = 0;
            if (g_Renderer.m_SSS_IgnoreEdgePixels) flags |= 0x1;
            // UsePrecisionOffset = off (bit 1)
            // BilinearSamplingOffsetMode = off (bit 2)
            if (g_Renderer.m_SSS_UseEarlyOut)      flags |= 0x8;

            for (int d = 0; d < dispatchList.DispatchCount; d++)
            {
                const Bend::DispatchData& dd = dispatchList.Dispatch[d];

                // Build per-dispatch constant buffer
                const nvrhi::BufferDesc sssCBDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
                    sizeof(srrhi::ScreenSpaceShadowConstants), "ScreenSpaceShadowCB", 1);
                const nvrhi::BufferHandle sssCB = device->createBuffer(sssCBDesc);

                srrhi::ScreenSpaceShadowConstants sssCBData;
                sssCBData.SetLightCoordinate(Vector4{
                    dispatchList.LightCoordinate_Shader[0],
                    dispatchList.LightCoordinate_Shader[1],
                    dispatchList.LightCoordinate_Shader[2],
                    dispatchList.LightCoordinate_Shader[3]
                });
                sssCBData.SetWaveOffset(Vector2I{ dd.WaveOffset_Shader[0], dd.WaveOffset_Shader[1] });
                sssCBData.SetInvDepthTextureSize(Vector2{ 1.0f / (float)width, 1.0f / (float)height });
                sssCBData.SetFarDepthValue(0.0f);   // Reversed-Z: far = 0
                sssCBData.SetNearDepthValue(1.0f);  // Reversed-Z: near = 1
                sssCBData.SetSurfaceThickness(g_Renderer.m_SSS_SurfaceThickness);
                sssCBData.SetBilinearThreshold(g_Renderer.m_SSS_BilinearThreshold);
                sssCBData.SetShadowContrast(g_Renderer.m_SSS_ShadowContrast);
                sssCBData.SetFlags(flags);
                sssCBData.SetDepthBounds(Vector2{ 0.0f, 1.0f }); // Full range for directional light

                commandList->writeBuffer(sssCB, &sssCBData, sizeof(sssCBData), 0);

                srrhi::ScreenSpaceShadowInputs sssInputs;
                sssInputs.SetCB(sssCB);
                sssInputs.SetDepthTexture(depth);
                sssInputs.SetOutputTexture(shadowMask, 0);
                sssInputs.SetPointBorderSampler(cr.ShadowSamplerPoint);

                Renderer::RenderPassParams sssParams;
                sssParams.commandList    = commandList;
                sssParams.shaderID       = ShaderID::SCREENSPACESHADOWS_SCREENSPACESHADOWS_CSMAIN_SS_SHADOW_SAMPLE_COUNT_60;
                sssParams.bindingSetDesc = Renderer::CreateBindingSetDesc(sssInputs);
                sssParams.dispatchParams = {
                    .x = (uint32_t)dd.WaveCount[0],
                    .y = (uint32_t)dd.WaveCount[1],
                    .z = (uint32_t)dd.WaveCount[2]
                };
                g_Renderer.AddComputePass(sssParams);
            }
        }
    }

    const char* GetName() const override { return "ShadowMask"; }
};

REGISTER_RENDERER(ShadowMaskRenderer);
