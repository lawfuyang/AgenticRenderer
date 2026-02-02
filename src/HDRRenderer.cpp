#include "Renderer.h"
#include "Config.h"
#include "Utilities.h"
#include "shaders/ShaderShared.h"

static constexpr float kMinLogLuminance = -12.47393f;
static constexpr float kMaxLogLuminance = 4.026069f;

class HDRRenderer : public IRenderer
{
public:
    bool Initialize() override
    {
        Renderer* renderer = Renderer::GetInstance();

        float initialExposure = 1.0f;
        ScopedCommandList cmd{ "Initialize HDR Exposure Buffer" };
        cmd->writeBuffer(renderer->m_ExposureBuffer, &initialExposure, sizeof(float));
        
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList) override
    {
        Renderer* renderer = Renderer::GetInstance();
        const ImGuiLayer& imgui = renderer->m_ImGuiLayer;

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
            nvrhi::BindingLayoutHandle layout = renderer->GetOrCreateBindingLayoutFromBindingSetDesc(bset);
            nvrhi::BindingSetHandle bindingSet = renderer->m_RHI->m_NvrhiDevice->createBindingSet(bset, layout);

            nvrhi::ComputeState state;
            state.pipeline = renderer->GetOrCreateComputePipeline(renderer->GetShaderHandle("LuminanceHistogram_LuminanceHistogram_CSMain"), layout);
            state.bindings = { bindingSet };

            commandList->setComputeState(state);
            commandList->setPushConstants(&consts, sizeof(consts));
            const uint32_t dispatchX = DivideAndRoundUp(consts.m_Width, 16);
            const uint32_t dispatchY = DivideAndRoundUp(consts.m_Height, 16);
            commandList->dispatch(dispatchX, dispatchY, 1);
        }

        // 2. Exposure Adaptation Pass
        {
            nvrhi::utils::ScopedMarker adaptMarker(commandList, "Exposure Adaptation");

            AdaptationConstants consts;
            consts.m_DeltaTime = (float)renderer->m_FrameTime / 1000.0f;
            consts.m_KeyValue = imgui.m_ExposureKeyValue;
            consts.m_AdaptationSpeed = imgui.m_AdaptationSpeed;
            consts.m_NumPixels = renderer->m_RHI->m_SwapchainExtent.x * renderer->m_RHI->m_SwapchainExtent.y;
            consts.m_MinLogLuminance = kMinLogLuminance;
            consts.m_MaxLogLuminance = kMaxLogLuminance;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(AdaptationConstants)),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, renderer->m_ExposureBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, renderer->m_LuminanceHistogram)
            };
            nvrhi::BindingLayoutHandle layout = renderer->GetOrCreateBindingLayoutFromBindingSetDesc(bset);
            nvrhi::BindingSetHandle bindingSet = renderer->m_RHI->m_NvrhiDevice->createBindingSet(bset, layout);

            nvrhi::ComputeState state;
            state.pipeline = renderer->GetOrCreateComputePipeline(renderer->GetShaderHandle("ExposureAdaptation_ExposureAdaptation_CSMain"), layout);
            state.bindings = { bindingSet };

            commandList->setComputeState(state);
            commandList->setPushConstants(&consts, sizeof(consts));
            commandList->dispatch(1, 1, 1);
        }

        // 3. Tonemapping Pass
        {
            nvrhi::utils::ScopedMarker tonemapMarker(commandList, "Tonemapping");

            TonemapConstants consts;
            consts.m_Width = renderer->m_RHI->m_SwapchainExtent.x;
            consts.m_Height = renderer->m_RHI->m_SwapchainExtent.y;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(TonemapConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, renderer->m_HDRColorTexture),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(1, renderer->m_ExposureBuffer)
            };
            nvrhi::BindingLayoutHandle layout = renderer->GetOrCreateBindingLayoutFromBindingSetDesc(bset);
            nvrhi::BindingSetHandle bindingSet = renderer->m_RHI->m_NvrhiDevice->createBindingSet(bset, layout);

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(renderer->GetCurrentBackBufferTexture());
            nvrhi::FramebufferHandle fb = renderer->m_RHI->m_NvrhiDevice->createFramebuffer(fbDesc);

            renderer->DrawFullScreenPass(
                commandList,
                fb,
                renderer->GetShaderHandle("Tonemap_Tonemap_PSMain"),
                { bindingSet },
                &consts,
                sizeof(consts)
            );
        }
    }

    const char* GetName() const override { return "HDRPostProcess"; }
};

REGISTER_RENDERER(HDRRenderer);
