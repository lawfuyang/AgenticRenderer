#include "Renderer.h"
#include "CommonResources.h"

#include "shaders/srrhi/cpp/Common.h"
#include "shaders/srrhi/cpp/ProbeTrace.h"
#include "shaders/srrhi/cpp/ProbeVis.h"

// DDGIRootConstants must be visible before DDGIBlending.h (srrhi static_assert).
// The SDK places the struct inside namespace rtxgi — hoist it to the global scope.
#include <rtxgi/ddgi/DDGIRootConstants.h>
#include "shaders/srrhi/cpp/DDGIBlending.h"

#include <rtxgi/ddgi/DDGIVolume.h>
#include <rtxgi/ddgi/DDGIVolumeDescGPU.h>
#include "shaders/srrhi/cpp/DDGIDebugCompositor.h"
#include <imgui.h>

// ---------------------------------------------------------------------------
// Render Graph handles
// ---------------------------------------------------------------------------
extern RGTextureHandle g_RG_DepthTexture;
extern RGTextureHandle g_RG_TAAOutput;

RGTextureHandle        g_RG_DDGIDebugOverlay;

// ---------------------------------------------------------------------------
// DDGIRenderer — DDGI probe ray tracing and blending (RTXGI-DDGI)
// Each DDGIVolumeNvrhi owns its textures, bindless indices, and buffers.
// The renderer loops over all volumes; nothing assumes a fixed volume count.
// Probe visualisation uses a shared sphere BLAS + per-volume TLAS instances.
// ---------------------------------------------------------------------------
class DDGIRenderer : public IRenderer
{
public:
    void PostSceneLoad() override
    {
        // Create 1 hardcoded DDGI volume around scene origin.
        rtxgi::DDGIVolumeDesc volDesc;
        volDesc.name                    = const_cast<char*>("DDGI Volume 0");
        volDesc.index                   = 0;
        volDesc.origin                  = { 0.0f, 5.0f, 0.0f };
        volDesc.eulerAngles             = { 0.0f, 0.0f, 0.0f };
        volDesc.probeSpacing            = { 1.5f, 1.5f, 1.5f };

        volDesc.probeCounts             = {
            static_cast<int>(ceilf(20.0f / 1.5f)),
            static_cast<int>(ceilf(10.0f / 1.5f)),
            static_cast<int>(ceilf(20.0f / 1.5f))
        };

        volDesc.probeNumRays                    = 256;
        volDesc.probeNumIrradianceInteriorTexels = 6;
        volDesc.probeNumDistanceInteriorTexels   = 14;
        volDesc.probeNumIrradianceTexels         = volDesc.probeNumIrradianceInteriorTexels + 2;
        volDesc.probeNumDistanceTexels           = volDesc.probeNumDistanceInteriorTexels + 2;

        volDesc.probeRayDataFormat     = rtxgi::EDDGIVolumeTextureFormat::F32x2;
        volDesc.probeIrradianceFormat  = rtxgi::EDDGIVolumeTextureFormat::U32;
        volDesc.probeDistanceFormat    = rtxgi::EDDGIVolumeTextureFormat::F16x2;
        volDesc.probeDataFormat        = rtxgi::EDDGIVolumeTextureFormat::F16x4;
        volDesc.probeVariabilityFormat = rtxgi::EDDGIVolumeTextureFormat::F16;

        volDesc.probeRelocationEnabled      = true;
        volDesc.probeClassificationEnabled  = true;
        volDesc.probeVariabilityEnabled     = true;

        DDGIVolumeNvrhi volume;
        volume.InitFromDesc(volDesc);
        volume.Update();
        ComputeVolumeTextureDims(volDesc, volume);

        nvrhi::DeviceHandle device = g_Renderer.m_RHI->m_NvrhiDevice;

        // Per-volume resource indices buffer
        {
            nvrhi::BufferDesc desc;
            desc.byteSize           = sizeof(rtxgi::DDGIVolumeResourceIndices);
            desc.structStride       = sizeof(rtxgi::DDGIVolumeResourceIndices);
            desc.debugName          = "DDGIResourceIndices_V0";
            desc.initialState       = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState   = true;
            volume.m_ResourceIndicesBuffer = device->createBuffer(desc);
        }
        volume.m_VolumeResourceIndicesIndex = g_Renderer.RegisterStructuredBufferSRV(
            volume.m_ResourceIndicesBuffer);

        g_Renderer.m_Scene.m_DDGIVolumes.push_back(volume);

        // DDGIVolumes structured buffer (one entry per volume)
        const size_t numVolumes = g_Renderer.m_Scene.m_DDGIVolumes.size();
        {
            nvrhi::BufferDesc desc;
            desc.byteSize           = numVolumes * sizeof(rtxgi::DDGIVolumeDescGPUPacked);
            desc.structStride       = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
            desc.debugName          = "DDGIVolumesBuffer";
            desc.initialState       = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState   = true;
            m_DDGIVolumesBuffer = device->createBuffer(desc);
        }
        m_VolumeConstantsIndex = g_Renderer.RegisterStructuredBufferSRV(m_DDGIVolumesBuffer);
        for (DDGIVolumeNvrhi& vol : g_Renderer.m_Scene.m_DDGIVolumes)
        {
            vol.m_VolumeConstantsIndex = m_VolumeConstantsIndex;
        }

        // ── Probe-vis TLAS + instance buffer (global across volumes) ─────────
        for (const DDGIVolumeNvrhi& v : g_Renderer.m_Scene.m_DDGIVolumes)
        {
            m_MaxProbes += static_cast<uint32_t>(
                v.GetDesc().probeCounts.x * v.GetDesc().probeCounts.y * v.GetDesc().probeCounts.z);
        }

        {
            nvrhi::rt::AccelStructDesc d;
            d.isTopLevel             = true;
            d.topLevelMaxInstances   = m_MaxProbes;
            d.debugName              = "DDGIProbeTLAS";
            d.buildFlags             = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
            m_ProbeTLAS = device->createAccelStruct(d);
        }
        {
            nvrhi::BufferDesc d;
            d.byteSize                = m_MaxProbes * sizeof(nvrhi::rt::InstanceDesc);
            d.structStride            = sizeof(nvrhi::rt::InstanceDesc);
            d.debugName               = "DDGIProbeInstanceDesc";
            d.isAccelStructBuildInput = true;
            d.canHaveUAVs             = true;   // needed for writeBuffer before TLAS build
            d.initialState            = nvrhi::ResourceStates::AccelStructBuildInput;
            d.keepInitialState        = true;
            m_ProbeInstanceBuffer = device->createBuffer(d);
        }

        // Logging
        const UINT totalProbes = static_cast<UINT>(
            volDesc.probeCounts.x * volDesc.probeCounts.y * volDesc.probeCounts.z);
        UINT probeCountX, probeCountY, probeCountZ;
        rtxgi::GetDDGIVolumeProbeCounts(volDesc, probeCountX, probeCountY, probeCountZ);
        SDL_Log("[DDGI] Volume created: %dx%dx%d, spacing %.1fm, origin (%.1f, %.1f, %.1f)",
                probeCountX, probeCountY, probeCountZ,
                volDesc.probeSpacing.x, volDesc.origin.x, volDesc.origin.y, volDesc.origin.z);
        SDL_Log("[DDGI]   Irr:%ux%ux%u  Dist:%ux%ux%u  Data:%ux%ux%u",
                volume.m_IrradianceTexDim.x, volume.m_IrradianceTexDim.y, volume.m_IrradianceTexDim.z,
                volume.m_DistanceTexDim.x,   volume.m_DistanceTexDim.y,   volume.m_DistanceTexDim.z,
                volume.m_ProbeDataTexDim.x,   volume.m_ProbeDataTexDim.y,   volume.m_ProbeDataTexDim.z);
        SDL_Log("[DDGI]   RayData:%ux%ux%u (%u probes x %u rays)  Variability:%ux%ux%u",
                volume.m_RayDataTexDim.x, volume.m_RayDataTexDim.y, volume.m_RayDataTexDim.z,
                totalProbes, volDesc.probeNumRays,
                volume.m_VariabilityTexDim.x, volume.m_VariabilityTexDim.y, volume.m_VariabilityTexDim.z);
        SDL_Log("[DDGI]   Textures from render graph. VolCIdx=%u  ResIdx=%u  MaxProbes=%u",
                volume.m_VolumeConstantsIndex, volume.m_VolumeResourceIndicesIndex, m_MaxProbes);
        SDL_Log("[DDGI]   Encoding: irradianceGamma=%.1f  irradianceFormat=%s",
                volDesc.probeIrradianceEncodingGamma,
                volDesc.probeIrradianceFormat == rtxgi::EDDGIVolumeTextureFormat::U32 ? "U32" : "UNORM");
        SDL_Log("[DDGI]   Relocation=%s  Classification=%s  Variability=%s",
                volDesc.probeRelocationEnabled     ? "ON" : "OFF",
                volDesc.probeClassificationEnabled ? "ON" : "OFF",
                volDesc.probeVariabilityEnabled    ? "ON" : "OFF");
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Renderer.m_EnableDDGIProbeTracing)
        {
            return false;
        }

        renderGraph.ReadTexture(g_RG_DepthTexture);

        for (DDGIVolumeNvrhi& vol : g_Renderer.m_Scene.m_DDGIVolumes)
        {
            declareTexture(renderGraph, vol.m_IrradianceTexture,       vol.m_IrradianceTexDim,    nvrhi::Format::R10G10B10A2_UNORM, "DDGI Irradiance");
            declareTexture(renderGraph, vol.m_DistanceTexture,         vol.m_DistanceTexDim,      nvrhi::Format::RG16_FLOAT,        "DDGI Distance");
            declareTexture(renderGraph, vol.m_ProbeDataTexture,        vol.m_ProbeDataTexDim,     nvrhi::Format::RGBA16_FLOAT,      "DDGI Probe Data");
            declareTexture(renderGraph, vol.m_RayDataTexture,          vol.m_RayDataTexDim,       nvrhi::Format::RG32_FLOAT,        "DDGI Ray Data");
            declareTexture(renderGraph, vol.m_ProbeVariabilityTexture, vol.m_VariabilityTexDim,   nvrhi::Format::R16_FLOAT,         "DDGI Probe Variability");

            // VariabilityAverage: per-volume 1×1 × arraySize
            RGTextureDesc desc;
            desc.m_NvrhiDesc.width        = std::max(1u, vol.m_VariabilityAvgTexDim.x);
            desc.m_NvrhiDesc.height       = std::max(1u, vol.m_VariabilityAvgTexDim.y);
            desc.m_NvrhiDesc.arraySize    = std::max(1u, vol.m_VariabilityAvgTexDim.z);
            desc.m_NvrhiDesc.dimension    = nvrhi::TextureDimension::Texture2DArray;
            desc.m_NvrhiDesc.format       = nvrhi::Format::R16_FLOAT;
            desc.m_NvrhiDesc.isUAV        = true;
            desc.m_NvrhiDesc.debugName    = "DDGI Probe Variability Average";
            desc.m_NvrhiDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
            desc.m_NvrhiDesc.keepInitialState = true;
            renderGraph.DeclarePersistentTexture(desc, vol.m_ProbeVariabilityAvgTexture);
        }

        // Per-frame overlay output (transient)
        {
            auto [w, h] = g_Renderer.SwapchainSize();
            RGTextureDesc d;
            d.m_NvrhiDesc.width         = w;
            d.m_NvrhiDesc.height        = h;
            d.m_NvrhiDesc.format        = nvrhi::Format::RGBA16_FLOAT;
            d.m_NvrhiDesc.isUAV         = true;
            d.m_NvrhiDesc.debugName     = "DDGIProbeOverlay";
            d.m_NvrhiDesc.initialState  = nvrhi::ResourceStates::UnorderedAccess;
            d.m_NvrhiDesc.keepInitialState = true;
            renderGraph.DeclareTexture(d, g_RG_DDGIDebugOverlay);
        }
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        PROFILE_FUNCTION();
        nvrhi::DeviceHandle device = g_Renderer.m_RHI->m_NvrhiDevice;
        auto& volumes = g_Renderer.m_Scene.m_DDGIVolumes;
        if (volumes.empty())
        {
            return;
        }

        // First frame: register all volume textures in the bindless heap
        for (DDGIVolumeNvrhi& vol : volumes)
        {
            ResolveAndRegisterBindless(renderGraph, commandList, vol);
        }

        // Update volume transforms
        for (DDGIVolumeNvrhi& vol : volumes)
        {
            vol.Update();
        }

        // Upload packed GPU descriptors
        for (size_t i = 0; i < volumes.size(); ++i)
        {
            rtxgi::DDGIVolumeDescGPUPacked packed = volumes[i].GetDescGPUPacked();
            commandList->writeBuffer(m_DDGIVolumesBuffer, &packed, sizeof(packed),
                                     static_cast<uint64_t>(i) * sizeof(packed));
        }

        // Probe trace + SDK blending per volume
        for (DDGIVolumeNvrhi& vol : volumes)
        {
            DispatchProbeTrace(commandList, device, renderGraph, vol);
            DispatchSDKBlendingPasses(commandList, vol);
        }

        // RT probe visualization (sphere instances via RayQuery)
        DispatchProbeVis(commandList, renderGraph);
    }

    const char* GetName() const override { return "DDGIRenderer"; }

private:
    // ── Helpers ─────────────────────────────────────────────────────────────

    static void ComputeVolumeTextureDims(const rtxgi::DDGIVolumeDesc& desc, DDGIVolumeNvrhi& vol)
    {
        UINT w, h, a;

        rtxgi::GetDDGIVolumeTextureDimensions(desc, rtxgi::EDDGIVolumeTextureType::Irradiance,  w, h, a);
        vol.m_IrradianceTexDim = { w, h, a };

        rtxgi::GetDDGIVolumeTextureDimensions(desc, rtxgi::EDDGIVolumeTextureType::Distance,    w, h, a);
        vol.m_DistanceTexDim = { w, h, a };

        rtxgi::GetDDGIVolumeTextureDimensions(desc, rtxgi::EDDGIVolumeTextureType::Data,        w, h, a);
        vol.m_ProbeDataTexDim = { w, h, a };

        rtxgi::GetDDGIVolumeTextureDimensions(desc, rtxgi::EDDGIVolumeTextureType::RayData,     w, h, a);
        vol.m_RayDataTexDim = { w, h, a };

        rtxgi::GetDDGIVolumeTextureDimensions(desc, rtxgi::EDDGIVolumeTextureType::Variability, w, h, a);
        vol.m_VariabilityTexDim = { w, h, a };

        rtxgi::GetDDGIVolumeTextureDimensions(desc, rtxgi::EDDGIVolumeTextureType::VariabilityAverage, w, h, a);
        vol.m_VariabilityAvgTexDim = { w, h, a };
    }

    static void declareTexture(RenderGraph& renderGraph, RGTextureHandle& h, const Vector3U& dim,
                               nvrhi::Format fmt, const char* name)
    {
        RGTextureDesc d;
        d.m_NvrhiDesc.width         = dim.x;
        d.m_NvrhiDesc.height        = dim.y;
        d.m_NvrhiDesc.arraySize     = dim.z;
        d.m_NvrhiDesc.dimension     = nvrhi::TextureDimension::Texture2DArray;
        d.m_NvrhiDesc.format        = fmt;
        d.m_NvrhiDesc.isUAV         = true;
        d.m_NvrhiDesc.debugName     = name;
        d.m_NvrhiDesc.initialState  = nvrhi::ResourceStates::UnorderedAccess;
        d.m_NvrhiDesc.keepInitialState = true;
        renderGraph.DeclarePersistentTexture(d, h);
    }

    static void ResolveAndRegisterBindless(const RenderGraph& renderGraph, nvrhi::CommandListHandle commandList,
                                           DDGIVolumeNvrhi& vol)
    {
        if (vol.m_bBindlessRegistered)
        {
            return;
        }

        nvrhi::TextureHandle rd = renderGraph.GetTexture(vol.m_RayDataTexture,     RGResourceAccessMode::Write);
        nvrhi::TextureHandle ir = renderGraph.GetTexture(vol.m_IrradianceTexture,  RGResourceAccessMode::Write);
        nvrhi::TextureHandle di = renderGraph.GetTexture(vol.m_DistanceTexture,    RGResourceAccessMode::Write);
        nvrhi::TextureHandle pd = renderGraph.GetTexture(vol.m_ProbeDataTexture,   RGResourceAccessMode::Write);
        nvrhi::TextureHandle va = renderGraph.GetTexture(vol.m_ProbeVariabilityTexture, RGResourceAccessMode::Write);
        nvrhi::TextureHandle vg = renderGraph.GetTexture(vol.m_ProbeVariabilityAvgTexture, RGResourceAccessMode::Write);

        vol.m_RayDataSRVIndex           = g_Renderer.RegisterTexture(rd);
        vol.m_RayDataUAVIndex           = g_Renderer.RegisterTextureUAV(rd);
        vol.m_IrradianceSRVIndex        = g_Renderer.RegisterTexture(ir);
        vol.m_IrradianceUAVIndex        = g_Renderer.RegisterTextureUAV(ir);
        vol.m_DistanceSRVIndex          = g_Renderer.RegisterTexture(di);
        vol.m_DistanceUAVIndex          = g_Renderer.RegisterTextureUAV(di);
        vol.m_ProbeDataSRVIndex         = g_Renderer.RegisterTexture(pd);
        vol.m_ProbeDataUAVIndex         = g_Renderer.RegisterTextureUAV(pd);
        vol.m_VariabilitySRVIndex       = g_Renderer.RegisterTexture(va);
        vol.m_VariabilityUAVIndex       = g_Renderer.RegisterTextureUAV(va);
        vol.m_VariabilityAvgSRVIndex    = g_Renderer.RegisterTexture(vg);
        vol.m_VariabilityAvgUAVIndex    = g_Renderer.RegisterTextureUAV(vg);

        rtxgi::DDGIVolumeResourceIndices ri = {};
        ri.rayDataUAVIndex                  = vol.m_RayDataUAVIndex;
        ri.rayDataSRVIndex                  = vol.m_RayDataSRVIndex;
        ri.probeIrradianceUAVIndex          = vol.m_IrradianceUAVIndex;
        ri.probeIrradianceSRVIndex          = vol.m_IrradianceSRVIndex;
        ri.probeDistanceUAVIndex            = vol.m_DistanceUAVIndex;
        ri.probeDistanceSRVIndex            = vol.m_DistanceSRVIndex;
        ri.probeDataUAVIndex                = vol.m_ProbeDataUAVIndex;
        ri.probeDataSRVIndex                = vol.m_ProbeDataSRVIndex;
        ri.probeVariabilityUAVIndex         = vol.m_VariabilityUAVIndex;
        ri.probeVariabilitySRVIndex         = vol.m_VariabilitySRVIndex;
        ri.probeVariabilityAverageUAVIndex  = vol.m_VariabilityAvgUAVIndex;
        ri.probeVariabilityAverageSRVIndex  = vol.m_VariabilityAvgSRVIndex;

        commandList->writeBuffer(vol.m_ResourceIndicesBuffer, &ri, sizeof(ri), 0);
        vol.m_bBindlessRegistered = true;

        SDL_Log("[DDGI] Bindless: IrrUAV=%u IrrSRV=%u DistUAV=%u DistSRV=%u",
                vol.m_IrradianceUAVIndex, vol.m_IrradianceSRVIndex,
                vol.m_DistanceUAVIndex, vol.m_DistanceSRVIndex);
    }

    // ── Probe trace ─────────────────────────────────────────────────────────

    void DispatchProbeTrace(nvrhi::CommandListHandle commandList, nvrhi::DeviceHandle device,
                            const RenderGraph& renderGraph, DDGIVolumeNvrhi& vol) const
    {
        rtxgi::DDGIVolumeDesc desc = vol.GetDesc();
        uint32_t totalProbes = static_cast<uint32_t>(
            desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z);
        uint32_t totalRays   = totalProbes * static_cast<uint32_t>(desc.probeNumRays);
        uint32_t groupsX     = (totalRays + 63u) / 64u;

        nvrhi::BufferHandle cb = device->createBuffer(
            nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(srrhi::ProbeTraceConstants), "ProbeTraceCB", 1));

        srrhi::ProbeTraceConstants constants;
        constants.SetSunDirection(g_Renderer.m_Scene.GetSunDirection());
        constants.SetSunIntensity(g_Renderer.m_EnableSky ? g_Renderer.m_Scene.GetSunIntensity() : 0.0f);
        constants.SetFrame(g_Renderer.m_FrameNumber);
        constants.SetVolumeIndex(static_cast<uint32_t>(desc.index));
        constants.SetLightCount(g_Renderer.m_Scene.m_LightCount);
        constants.SetIrradianceTexIndex(vol.m_IrradianceSRVIndex);
        constants.SetDistanceTexIndex(vol.m_DistanceSRVIndex);
        constants.SetProbeDataTexIndex(vol.m_ProbeDataSRVIndex);
        commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

        srrhi::ProbeTraceInputs inputs;
        inputs.SetProbeTraceCB(cb);
        inputs.SetDDGIVolumes(m_DDGIVolumesBuffer);
        inputs.SetSceneAS(g_Renderer.m_Scene.m_TLAS);
        inputs.SetLights(g_Renderer.m_Scene.m_LightBuffer);
        inputs.SetInstances(g_Renderer.m_Scene.m_InstanceDataBuffer);
        inputs.SetMeshData(g_Renderer.m_Scene.m_MeshDataBuffer);
        inputs.SetMaterials(g_Renderer.m_Scene.m_MaterialConstantsBuffer);
        inputs.SetIndices(g_Renderer.m_Scene.m_IndexBuffer);
        inputs.SetVertices(g_Renderer.m_Scene.m_VertexBufferQuantized);
        inputs.SetProbeData(renderGraph.GetTexture(vol.m_ProbeDataTexture, RGResourceAccessMode::Write));
        inputs.SetRayData(renderGraph.GetTexture(vol.m_RayDataTexture, RGResourceAccessMode::Write), 0, 0, -1);

        Renderer::RenderPassParams params;
        params.commandList    = commandList;
        params.shaderID       = ShaderID::DDGI_PROBETRACECS_PROBETRACECS;
        params.bindingSetDesc = Renderer::CreateBindingSetDesc(inputs);
        params.dispatchParams = { groupsX, 1, 1 };
        g_Renderer.AddComputePass(params);
    }

    // ── SDK blending ────────────────────────────────────────────────────────

    void DispatchSDKPass(nvrhi::CommandListHandle commandList, const DDGIVolumeNvrhi& vol,
                         uint32_t shaderID, uint32_t gx, uint32_t gy, uint32_t gz,
                         uint32_t rX = 0, uint32_t rY = 0, uint32_t rZ = 0) const
    {
        rtxgi::DDGIRootConstants rc = {};
        rc.volumeIndex                = static_cast<uint32_t>(vol.GetDesc().index);
        rc.volumeConstantsIndex       = vol.m_VolumeConstantsIndex;
        rc.volumeResourceIndicesIndex = vol.m_VolumeResourceIndicesIndex;
        rc.reductionInputSizeX        = rX;
        rc.reductionInputSizeY        = rY;
        rc.reductionInputSizeZ        = rZ;

        srrhi::DDGIBlendingInputs inputs;
        inputs.m_DDGIRootCB.SetRootConstants(rc);

        Renderer::RenderPassParams params;
        params.commandList             = commandList;
        params.shaderID                = shaderID;
        params.bindingSetDesc          = Renderer::CreateBindingSetDesc(inputs);
        params.pushConstants           = &inputs.m_DDGIRootCB;
        params.pushConstantsSize       = srrhi::DDGIBlendingInputs::PushConstantBytes;
        params.dispatchParams          = { gx, gy, gz };
        params.bIncludeBindlessResources = true;
        g_Renderer.AddComputePass(params);
    }

    void DispatchSDKBlendingPasses(nvrhi::CommandListHandle commandList, const DDGIVolumeNvrhi& vol) const
    {
        rtxgi::DDGIVolumeDesc desc = vol.GetDesc();
        uint32_t N = static_cast<uint32_t>(desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z);

        DispatchSDKPass(commandList, vol,
                        ShaderID::DDGI_DDGIPROBEBLENDINGIRRADIANCE_DDGIPROBEBLENDINGCS,
                        static_cast<uint32_t>(desc.probeCounts.x),
                        static_cast<uint32_t>(desc.probeCounts.y),
                        static_cast<uint32_t>(desc.probeCounts.z));

        DispatchSDKPass(commandList, vol,
                        ShaderID::DDGI_DDGIPROBEBLENDINGDISTANCE_DDGIPROBEBLENDINGCS_BLENDDISTANCE,
                        static_cast<uint32_t>(desc.probeCounts.x),
                        static_cast<uint32_t>(desc.probeCounts.y),
                        static_cast<uint32_t>(desc.probeCounts.z));

        if (desc.probeRelocationEnabled)
        {
            const uint32_t relocGroupsX = (N + 31u) / 32u;
            if (desc.probeRelocationNeedsReset)
            {
                DispatchSDKPass(commandList, vol,
                                ShaderID::DDGI_DDGIPROBERELOCATIONRESET_DDGIPROBERELOCATIONRESETCS_RESET,
                                relocGroupsX, 1u, 1u);
            }
            DispatchSDKPass(commandList, vol,
                            ShaderID::DDGI_DDGIPROBERELOCATION_DDGIPROBERELOCATIONCS,
                            relocGroupsX, 1u, 1u);
        }

        if (desc.probeClassificationEnabled)
        {
            const uint32_t classGroupsX = (N + 31u) / 32u;
            if (desc.probeClassificationNeedsReset)
            {
                DispatchSDKPass(commandList, vol,
                                ShaderID::DDGI_DDGIPROBECLASSIFICATIONRESET_DDGIPROBECLASSIFICATIONRESETCS_RESET,
                                classGroupsX, 1u, 1u);
            }
            DispatchSDKPass(commandList, vol,
                            ShaderID::DDGI_DDGIPROBECLASSIFICATION_DDGIPROBECLASSIFICATIONCS,
                            classGroupsX, 1u, 1u);
        }

        if (desc.probeVariabilityEnabled)
        {
            const uint32_t varW = vol.m_VariabilityTexDim.x;
            const uint32_t varH = vol.m_VariabilityTexDim.y;
            const uint32_t varZ = vol.m_VariabilityTexDim.z;

            const uint32_t redGroupsX = (varW + 3u) / 4u;
            const uint32_t redGroupsY = (varH + 7u) / 8u;
            const uint32_t redGroupsZ = (varZ + 3u) / 4u;

            DispatchSDKPass(commandList, vol,
                            ShaderID::DDGI_DDGIREDUCTION_DDGIREDUCTIONCS,
                            redGroupsX, redGroupsY, redGroupsZ,
                            varW, varH, varZ);

            const uint32_t extraRedGroupsX = (redGroupsX + 3u) / 4u;
            const uint32_t extraRedGroupsY = (redGroupsY + 7u) / 8u;
            const uint32_t extraRedGroupsZ = (redGroupsZ + 3u) / 4u;

            DispatchSDKPass(commandList, vol,
                            ShaderID::DDGI_DDGIREDUCTIONEXTRA_DDGIEXTRAREDUCTIONCS_EXTRA,
                            std::max(1u, extraRedGroupsX),
                            std::max(1u, extraRedGroupsY),
                            std::max(1u, extraRedGroupsZ),
                            redGroupsX, redGroupsY, redGroupsZ);
        }
    }

    // ── Probe vis ───────────────────────────────────────────────────────────

    void DispatchProbeVis(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) const
    {
        const uint32_t debugMode = g_Renderer.m_DDGIDebugMode;
        if (debugMode == srrhi::DDGIDebugMode::DDGI_DEBUG_OFF)
        {
            return;
        }

        auto& volumes = g_Renderer.m_Scene.m_DDGIVolumes;
        if (volumes.empty() || m_MaxProbes == 0)
        {
            return;
        }

        nvrhi::TextureHandle overlay = renderGraph.GetTexture(g_RG_DDGIDebugOverlay, RGResourceAccessMode::Write);
        commandList->clearTextureFloat(overlay, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });

        auto [width, height] = g_Renderer.SwapchainSize();

        // Build instance descs on CPU using probe grid positions
        std::vector<nvrhi::rt::InstanceDesc> instances;
        instances.reserve(m_MaxProbes);
        uint64_t blasAddr = CommonResources::GetInstance().UnitSphereMesh.m_BLAS->getDeviceAddress();
        const float kProbeRadius = 0.15f;

        for (const DDGIVolumeNvrhi& vol : volumes)
        {
            rtxgi::DDGIVolumeDesc desc = vol.GetDesc();
            uint32_t numProbes = static_cast<uint32_t>(
                desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z);

            // RTXGI SDK centres the probe volume about its origin.
            // DDGIGetProbeWorldPosition:  pos = coords*spacing - shift + origin
            // where shift = spacing * (counts-1) * 0.5
            const Vector3 shift = {
                desc.probeSpacing.x * (desc.probeCounts.x - 1) * 0.5f,
                desc.probeSpacing.y * (desc.probeCounts.y - 1) * 0.5f,
                desc.probeSpacing.z * (desc.probeCounts.z - 1) * 0.5f
            };

            for (uint32_t i = 0; i < numProbes; ++i)
            {
                const int px = i % desc.probeCounts.x;
                const int py = (i / desc.probeCounts.x) % desc.probeCounts.y;
                const int pz = i / (desc.probeCounts.x * desc.probeCounts.y);

                Vector3 gridPos = {
                    desc.origin.x + (float)px * desc.probeSpacing.x - shift.x,
                    desc.origin.y + (float)py * desc.probeSpacing.y - shift.y,
                    desc.origin.z + (float)pz * desc.probeSpacing.z - shift.z
                };

                nvrhi::rt::InstanceDesc inst;
                float* t = inst.transform;
                t[0]  = kProbeRadius;
                t[1]  = 0.0f;
                t[2]  = 0.0f;
                t[3]  = gridPos.x;
                t[4]  = 0.0f;
                t[5]  = kProbeRadius;
                t[6]  = 0.0f;
                t[7]  = gridPos.y;
                t[8]  = 0.0f;
                t[9]  = 0.0f;
                t[10] = kProbeRadius;
                t[11] = gridPos.z;

                inst.setInstanceID(static_cast<uint32_t>((vol.GetDesc().index << 16) | i));
                inst.setInstanceMask(0xFF);
                inst.setFlags(nvrhi::rt::InstanceFlags::None);
                inst.blasDeviceAddress = blasAddr;
                instances.push_back(inst);
            }
        }

        commandList->writeBuffer(m_ProbeInstanceBuffer, instances.data(),
                                 instances.size() * sizeof(nvrhi::rt::InstanceDesc));
        commandList->buildTopLevelAccelStructFromBuffer(m_ProbeTLAS, m_ProbeInstanceBuffer, 0,
                                                        static_cast<uint32_t>(instances.size()));

        // Build camera CB for ProbeVisCS
        nvrhi::BufferHandle cb = g_Renderer.m_RHI->m_NvrhiDevice->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(srrhi::ProbeVisConstants), "ProbeVisCB", 1));

        // Pass the full view-to-world matrix — HLSL extracts columns natively in column-major.
        srrhi::ProbeVisConstants vis;
        vis.SetMatViewToWorld(g_Renderer.m_Scene.m_View.m_MatViewToWorld);
        vis.SetMatWorldToClip(g_Renderer.m_Scene.m_View.m_MatWorldToClip);
        vis.SetMatClipToWorld(g_Renderer.m_Scene.m_View.m_MatClipToWorld);

        // Extract tanHalfFovY from the projection matrix.
        // Standard perspective: P[1][1] = 1 / tan(fovY/2). Stored row-major: _22.
        const float tanHalfFovY = 1.0f / g_Renderer.m_Scene.m_View.m_MatViewToClip._22;
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        vis.SetTanHalfFovY(tanHalfFovY);
        vis.SetAspect(aspect);
        vis.SetDebugMode(debugMode);
        vis.SetViewportWidth(static_cast<float>(width));
        vis.SetViewportSize({ static_cast<float>(width), static_cast<float>(height) });
        vis.SetViewportSizeInv({ 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height) });

        // Bindless indices from the first volume
        const DDGIVolumeNvrhi& visVolume = g_Renderer.m_Scene.m_DDGIVolumes[0];
        vis.SetIrradianceTexIndex(visVolume.m_IrradianceSRVIndex);
        vis.SetDistanceTexIndex(visVolume.m_DistanceSRVIndex);
        vis.SetProbeDataTexIndex(visVolume.m_ProbeDataSRVIndex);

        commandList->writeBuffer(cb, &vis, sizeof(vis), 0);

        nvrhi::TextureHandle depthTex = renderGraph.GetTexture(g_RG_DepthTexture, RGResourceAccessMode::Read);

        srrhi::ProbeVisInputs inputs;
        inputs.SetProbeVisCB(cb);
        inputs.SetProbeTLAS(m_ProbeTLAS);
        inputs.SetDDGIVolumes(m_DDGIVolumesBuffer);
        inputs.SetDepth(depthTex);
        inputs.SetOverlayOutput(overlay, 0);

        const uint32_t groupsX = (width  + 7u) / 8u;
        const uint32_t groupsY = (height + 7u) / 8u;

        Renderer::RenderPassParams params;
        params.commandList             = commandList;
        params.shaderID                = ShaderID::DDGI_PROBEVISCS_PROBEVISCS;
        params.bindingSetDesc          = Renderer::CreateBindingSetDesc(inputs);
        params.dispatchParams          = { groupsX, groupsY, 1 };
        params.bIncludeBindlessResources = true;
        g_Renderer.AddComputePass(params);
    }

    // ── Members ─────────────────────────────────────────────────────────────
    nvrhi::BufferHandle              m_DDGIVolumesBuffer;
    uint32_t                         m_VolumeConstantsIndex = UINT32_MAX;
    uint32_t                         m_MaxProbes            = 0;
    nvrhi::rt::AccelStructHandle     m_ProbeTLAS;
    nvrhi::BufferHandle              m_ProbeInstanceBuffer;
};

REGISTER_RENDERER(DDGIRenderer)

// ──────────────────────────────────────────────────────────────────────────────
// DDGI Debug Compositor — additive-blends the DDGI debug overlay onto the
// post-bloom HDR output.  Registered in the same file for locality.

class DDGIDebugCompositor : public IRenderer
{
public:
    const char* GetName() const override { return "DDGIDebugCompositor"; }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (g_Renderer.m_DDGIDebugMode == srrhi::DDGIDebugMode::DDGI_DEBUG_OFF || !g_Renderer.m_EnableDDGIProbeTracing)
            return false;

        renderGraph.ReadTexture(g_RG_DDGIDebugOverlay);
        renderGraph.WriteTexture(g_RG_TAAOutput);
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        nvrhi::TextureHandle dst     = renderGraph.GetTexture(g_RG_TAAOutput,        RGResourceAccessMode::Write);
        nvrhi::TextureHandle overlay = renderGraph.GetTexture(g_RG_DDGIDebugOverlay, RGResourceAccessMode::Read);

        srrhi::DDGIDebugCompositorInputs inputs;
        inputs.SetDDGIDebugOverlay(overlay);
        inputs.SetOutput(dst, 0);

        auto [width, height] = g_Renderer.SwapchainSize();
        uint32_t groupsX = (width  + 7) / 8;
        uint32_t groupsY = (height + 7) / 8;

        Renderer::RenderPassParams params;
        params.commandList    = commandList;
        params.shaderID       = ShaderID::DDGIDEBUGCOMPOSITOR_CSMAIN;
        params.bindingSetDesc = Renderer::CreateBindingSetDesc(inputs);
        params.dispatchParams = { groupsX, groupsY, 1 };
        g_Renderer.AddComputePass(params);
    }
};

REGISTER_RENDERER(DDGIDebugCompositor)
