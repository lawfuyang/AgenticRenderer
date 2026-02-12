#include "Renderer.h"
#include "Config.h"
#include "Utilities.h"
#include "CommonResources.h"
#include "shaders/ShaderShared.h"

static constexpr float kMinLogLuminance = -10.0f;
static constexpr float kMaxLogLuminance = 20.0f;

class HDRRenderer : public IRenderer
{
public:
    void Initialize() override
    {
        Renderer* renderer = Renderer::GetInstance();

        float initialExposure = 1.0f;
        nvrhi::CommandListHandle cmd = renderer->AcquireCommandList("Initialize HDR Exposure Buffer");
        ScopedCommandList scopedCmd{ cmd };
        scopedCmd->writeBuffer(renderer->m_ExposureBuffer, &initialExposure, sizeof(float));
    }

    
    void Render(nvrhi::CommandListHandle commandList) override
    {
        Renderer* renderer = Renderer::GetInstance();

        nvrhi::utils::ScopedMarker marker(commandList, "HDR Post-Processing");

        // 1. Histogram Pass
        {
            nvrhi::utils::ScopedMarker histMarker(commandList, "Luminance Histogram");
            
            // Clear histogram buffer
            commandList->clearBufferUInt(renderer->m_LuminanceHistogram, 0);

            HistogramConstants consts;
            consts.m_Width = renderer->m_RHI->m_SwapchainExtent.x;
            consts.m_Height = renderer->m_RHI->m_SwapchainExtent.y;
            consts.m_MinLogLuminance = kMinLogLuminance;
            consts.m_MaxLogLuminance = kMaxLogLuminance;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(HistogramConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, renderer->m_HDRColorTexture),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, renderer->m_LuminanceHistogram)
            };

            const uint32_t dispatchX = DivideAndRoundUp(consts.m_Width, 16);
            const uint32_t dispatchY = DivideAndRoundUp(consts.m_Height, 16);

            Renderer::RenderPassParams params{
                .commandList = commandList,
                .shaderName = "LuminanceHistogram_LuminanceHistogram_CSMain",
                .bindingSetDesc = bset,
                .pushConstants = &consts,
                .pushConstantsSize = sizeof(consts),
                .dispatchParams = { .x = dispatchX, .y = dispatchY, .z = 1 }
            };

            renderer->AddComputePass(params);
        }

        // 2. Exposure Adaptation Pass
        {
            nvrhi::utils::ScopedMarker adaptMarker(commandList, "Exposure Adaptation");

            if (renderer->m_EnableAutoExposure)
            {
                AdaptationConstants consts;
                consts.m_DeltaTime = (float)renderer->m_FrameTime / 1000.0f;
                consts.m_AdaptationSpeed = renderer->m_AdaptationSpeed;
                consts.m_NumPixels = renderer->m_RHI->m_SwapchainExtent.x * renderer->m_RHI->m_SwapchainExtent.y;
                consts.m_MinLogLuminance = kMinLogLuminance;
                consts.m_MaxLogLuminance = kMaxLogLuminance;
                consts.m_ExposureValueMin = renderer->m_Camera.m_ExposureValueMin;
                consts.m_ExposureValueMax = renderer->m_Camera.m_ExposureValueMax;
                consts.m_ExposureCompensation = renderer->m_Camera.m_ExposureCompensation;

                nvrhi::BindingSetDesc bset;
                bset.bindings = {
                    nvrhi::BindingSetItem::PushConstants(0, sizeof(AdaptationConstants)),
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(0, renderer->m_ExposureBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, renderer->m_LuminanceHistogram)
                };

                Renderer::RenderPassParams params{
                    .commandList = commandList,
                    .shaderName = "ExposureAdaptation_ExposureAdaptation_CSMain",
                    .bindingSetDesc = bset,
                    .pushConstants = &consts,
                    .pushConstantsSize = sizeof(consts),
                    .dispatchParams = { .x = 1, .y = 1, .z = 1 }
                };

                renderer->AddComputePass(params);
            }
            else
            {
                // Manual mode: just update the buffer from CPU
                commandList->writeBuffer(renderer->m_ExposureBuffer, &renderer->m_Camera.m_Exposure, sizeof(float));
            }
        }

        // 3. Tonemapping Pass
        {
            nvrhi::utils::ScopedMarker tonemapMarker(commandList, "Tonemapping");

            TonemapConstants consts;
            consts.m_Width = renderer->m_RHI->m_SwapchainExtent.x;
            consts.m_Height = renderer->m_RHI->m_SwapchainExtent.y;
            consts.m_BloomIntensity = renderer->m_BloomIntensity;
            consts.m_EnableBloom = (renderer->m_EnableBloom && renderer->m_BloomUpPyramid) ? 1 : 0;
            consts.m_DebugBloom = (renderer->m_DebugBloom) ? 1 : 0;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(TonemapConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, renderer->m_HDRColorTexture),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(1, renderer->m_ExposureBuffer),
                nvrhi::BindingSetItem::Texture_SRV(2, renderer->m_BloomUpPyramid),
                nvrhi::BindingSetItem::Sampler(0, CommonResources::GetInstance().LinearClamp)
            };

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(renderer->GetCurrentBackBufferTexture());
            nvrhi::FramebufferHandle fb = renderer->m_RHI->m_NvrhiDevice->createFramebuffer(fbDesc);

            Renderer::RenderPassParams params{
                .commandList = commandList,
                .shaderName = "Tonemap_Tonemap_PSMain",
                .bindingSetDesc = bset,
                .pushConstants = &consts,
                .pushConstantsSize = sizeof(consts),
                .framebuffer = fb
            };

            renderer->AddFullScreenPass(params);
        }
    }

    const char* GetName() const override { return "HDRPostProcess"; }
};

REGISTER_RENDERER(HDRRenderer);
