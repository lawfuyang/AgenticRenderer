#include "Renderer.h"
#include "CommonResources.h"

#include "shaders/srrhi/cpp/ShadowMask.h"

// ---------------------------------------------------------------------------
// Render Graph handles — g_RG_CSMShadowMap / g_RG_EVSMShadowMap defined in ShadowRenderer.cpp
// ---------------------------------------------------------------------------
extern RGTextureHandle g_RG_CSMShadowMap;
extern RGTextureHandle g_RG_EVSMShadowMap;
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

        if (g_Renderer.m_EnableEVSSM)
            renderGraph.ReadTexture(g_RG_EVSMShadowMap);

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
        cb.SetClipToView(g_Renderer.m_Scene.m_View.m_MatClipToViewNoOffset);   // no TAA jitter — contact shadow ray must be stable
        cb.SetWorldToClip(g_Renderer.m_Scene.m_View.m_MatWorldToClipNoOffset); // no TAA jitter — contact shadow ray must be stable
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

        // EVSSM fields
        cb.SetVsmExponent(g_Renderer.m_VsmExponent);
        cb.SetBulbRadius(g_Renderer.m_BulbRadius);
        cb.SetMaxMipLevel(floorf(log2f((float)srrhi::CommonConsts::kShadowMapResolution)));
        cb.SetMaxSearchRadius(g_Renderer.m_MaxSearchRadius);
        cb.SetPenumbraRatioScale(g_Renderer.m_PenumbraRatioScale);
        cb.SetMaxPenumbraRatio(g_Renderer.m_MaxPenumbraRatio);
        cb.SetLightBleedReduction(g_Renderer.m_LightBleedReduction);
        cb.SetProjectionParam(Vector4{
            g_Renderer.m_CSMCascades[0].m_SplitFar - g_Renderer.m_CSMCascades[0].m_SplitNear,
            g_Renderer.m_CSMCascades[1].m_SplitFar - g_Renderer.m_CSMCascades[1].m_SplitNear,
            g_Renderer.m_CSMCascades[2].m_SplitFar - g_Renderer.m_CSMCascades[2].m_SplitNear,
            g_Renderer.m_CSMCascades[3].m_SplitFar - g_Renderer.m_CSMCascades[3].m_SplitNear,
        });

        // Contact shadow fields
        cb.SetLightDirectionWS(g_Renderer.m_Scene.GetSunDirection());
        cb.SetContactShadowDistance(g_Renderer.m_ContactShadowDistance);
        cb.SetContactShadowSteps(g_Renderer.m_EnableContactShadows ? (uint32_t)g_Renderer.m_ContactShadowSteps : 0u);
        cb.SetContactShadowsUseBlueNoise(g_Renderer.m_ContactShadowsUseBlueNoise ? 1u : 0u);
        cb.SetFrameIndex(g_Renderer.m_FrameNumber);

        commandList->writeBuffer(shadowMaskCB, &cb, sizeof(cb), 0);

        const CommonResources& cr = CommonResources::GetInstance();

        // -----------------------------------------------------------------------
        // Resolve textures
        // -----------------------------------------------------------------------
        nvrhi::TextureHandle depth     = renderGraph.GetTexture(g_RG_DepthTexture,   RGResourceAccessMode::Read);
        nvrhi::TextureHandle normals   = renderGraph.GetTexture(g_RG_GBufferNormals, RGResourceAccessMode::Read);
        nvrhi::TextureHandle shadowMap = renderGraph.GetTexture(g_RG_CSMShadowMap,   RGResourceAccessMode::Read);
        nvrhi::TextureHandle evsmMap   = g_Renderer.m_EnableEVSSM
            ? renderGraph.GetTexture(g_RG_EVSMShadowMap, RGResourceAccessMode::Read)
            : cr.DummySRVFloat4Array;

        // -----------------------------------------------------------------------
        // Dispatch
        // -----------------------------------------------------------------------
        {
            srrhi::ShadowMaskInputs inputs;
            inputs.SetCB(shadowMaskCB);
            inputs.SetDepth(depth);
            inputs.SetGBufferNormals(normals);
            inputs.SetCSMShadowMap(shadowMap);
            inputs.SetEVSMShadowMap(evsmMap);
            inputs.SetBlueNoiseTexture(cr.BlueNoiseTexture);
            inputs.SetRWShadowMask(shadowMask, 0);
            inputs.SetShadowSampler(cr.ShadowComparison);
            inputs.SetLinearClamp(cr.LinearClamp);
            inputs.SetPointClamp(cr.PointClamp);
            inputs.SetPointWrap(cr.PointWrap);

            const bool bEVSSM = g_Renderer.m_EnableEVSSM;
            uint32_t shaderID = bEVSSM
                ? ShaderID::SHADOWMASK_SHADOWMASK_CSMAIN_EVSSM_EVSSM_1
                : ShaderID::SHADOWMASK_SHADOWMASK_CSMAIN;

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
    }

    const char* GetName() const override { return "ShadowMask"; }
};

REGISTER_RENDERER(ShadowMaskRenderer);
