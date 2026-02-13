#include "BasePassCommon.h"
#include "Renderer.h"
#include "Utilities.h"
#include "shaders/ShaderShared.h"

void BasePassResources::Initialize()
{
    Renderer* renderer = Renderer::GetInstance();

    // Create pipeline statistics queries for double buffering
    m_PipelineQueries[0] = renderer->m_RHI->m_NvrhiDevice->createPipelineStatisticsQuery();
    m_PipelineQueries[1] = renderer->m_RHI->m_NvrhiDevice->createPipelineStatisticsQuery();
}

void BasePassResources::DeclareResources(RenderGraph& rg)
{
    Renderer* renderer = Renderer::GetInstance();

    uint32_t numPrimitives = (uint32_t)renderer->m_Scene.m_InstanceData.size();
    numPrimitives = std::max(numPrimitives, 1u); // avoid zero-sized buffers

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(sizeof(uint32_t))
            .setStructStride(sizeof(uint32_t))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("VisibleCount");
        m_VisibleCountBuffer = rg.DeclareBuffer(desc, m_VisibleCountBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(sizeof(uint32_t))
            .setStructStride(sizeof(uint32_t))
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("OccludedCount");
        m_OccludedCountBuffer = rg.DeclareBuffer(desc, m_OccludedCountBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(sizeof(DispatchIndirectArguments))
            .setStructStride(sizeof(DispatchIndirectArguments))
            .setIsDrawIndirectArgs(true)
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("OccludedIndirectBuffer");
        m_OccludedIndirectBuffer = rg.DeclareBuffer(desc, m_OccludedIndirectBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(sizeof(uint32_t))
            .setStructStride(sizeof(uint32_t))
            .setIsDrawIndirectArgs(true)
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("MeshletJobCount");
        m_MeshletJobCountBuffer = rg.DeclareBuffer(desc, m_MeshletJobCountBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(numPrimitives * sizeof(nvrhi::DrawIndexedIndirectArguments))
            .setStructStride(sizeof(nvrhi::DrawIndexedIndirectArguments))
            .setIsDrawIndirectArgs(true)
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("VisibleIndirectBuffer");
        m_VisibleIndirectBuffer = rg.DeclareBuffer(desc, m_VisibleIndirectBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(numPrimitives * sizeof(uint32_t))
            .setStructStride(sizeof(uint32_t))
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("OccludedIndices");
        m_OccludedIndicesBuffer = rg.DeclareBuffer(desc, m_OccludedIndicesBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(numPrimitives * sizeof(DispatchIndirectArguments))
            .setStructStride(sizeof(DispatchIndirectArguments))
            .setIsDrawIndirectArgs(true)
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("MeshletIndirectBuffer");
        m_MeshletIndirectBuffer = rg.DeclareBuffer(desc, m_MeshletIndirectBuffer);
    }

    {
        RGBufferDesc desc;
        desc.m_NvrhiDesc.setByteSize(numPrimitives * sizeof(MeshletJob))
            .setStructStride(sizeof(MeshletJob))
            .setCanHaveUAVs(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
            .setDebugName("MeshletJobBuffer");
        m_MeshletJobBuffer = rg.DeclareBuffer(desc, m_MeshletJobBuffer);
    }
}
