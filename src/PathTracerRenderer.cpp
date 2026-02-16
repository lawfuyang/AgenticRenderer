#include "Renderer.h"
#include "CommonResources.h"
#include "Utilities.h"
#include "shaders/ShaderShared.h"

extern RGTextureHandle g_RG_HDRColor;

class PathTracerRenderer : public IRenderer
{
public:
    bool Setup(RenderGraph& renderGraph) override
    {
        Renderer* renderer = Renderer::GetInstance();
        if (!renderer->m_EnableReferencePathTracer)
            return false;

        renderGraph.WriteTexture(g_RG_HDRColor);
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        PROFILE_FUNCTION();
        Renderer* renderer = Renderer::GetInstance();

        nvrhi::TextureHandle hdrColor = renderGraph.GetTexture(g_RG_HDRColor, RGResourceAccessMode::Write);
        const nvrhi::TextureDesc& hdrDesc = hdrColor->getDesc();

        const nvrhi::BufferDesc pathTracerCBD = nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(PathTracerConstants), "PathTracerCB", 1);
        const nvrhi::BufferHandle pathTracerCB = renderer->m_RHI->m_NvrhiDevice->createBuffer(pathTracerCBD);

        PathTracerConstants cb{};
        cb.m_View = renderer->m_View;
        cb.m_CameraPos = Vector4{ renderer->m_Camera.GetPosition().x, renderer->m_Camera.GetPosition().y, renderer->m_Camera.GetPosition().z, 1.0f };
        cb.m_LightCount = renderer->m_Scene.m_LightCount;
        commandList->writeBuffer(pathTracerCB, &cb, sizeof(cb), 0);

        nvrhi::BindingSetDesc bset;
        bset.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, pathTracerCB),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, renderer->m_Scene.m_TLAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, renderer->m_Scene.m_LightBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, renderer->m_Scene.m_InstanceDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, renderer->m_Scene.m_MeshDataBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(4, renderer->m_Scene.m_MaterialConstantsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, renderer->m_Scene.m_IndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, renderer->m_Scene.m_VertexBufferQuantized),
            nvrhi::BindingSetItem::Texture_UAV(0, hdrColor)
        };

        Renderer::RenderPassParams params{
            .commandList = commandList,
            .shaderName = "PathTracer_PathTracer_CSMain",
            .bindingSetDesc = bset,
            .useBindlessResources = true,
            .dispatchParams = {
                .x = DivideAndRoundUp(hdrDesc.width, 8),
                .y = DivideAndRoundUp(hdrDesc.height, 8),
                .z = 1
            }
        };

        renderer->AddComputePass(params);
    }

    const char* GetName() const override { return "ReferencePathTracer"; }
};

REGISTER_RENDERER(PathTracerRenderer);
