#include "Renderer.h"
#include "CommonResources.h"

#include "shaders/srrhi/cpp/Common.h"
#include "shaders/srrhi/cpp/ProbeTrace.h"
#include "shaders/srrhi/cpp/ProbeOverlay.h"

#include <rtxgi/ddgi/DDGIVolume.h>
#include <rtxgi/ddgi/gfx/DDGIVolume_D3D12.h>
#include <imgui.h>

// ---------------------------------------------------------------------------
// Render Graph handles
// ---------------------------------------------------------------------------
extern RGTextureHandle g_RG_DepthTexture;

RGTextureHandle        g_RG_DDGIDebugOverlay;  // DDGI debug overlay (RGBA16_FLOAT, screen res, transient)

// ---------------------------------------------------------------------------
// DDGIRenderer — DDGI probe ray tracing and blending (RTXGI-DDGI)
// ---------------------------------------------------------------------------
class DDGIRenderer : public IRenderer
{
public:
    void PostSceneLoad() override
    {
        // Create 1 hardcoded DDGI volume around scene origin.
        // 20×10×20 meter AABB, 1.5m probe spacing.
        rtxgi::DDGIVolumeDesc vol;
        vol.name                    = const_cast<char*>("DDGI Volume 0");
        vol.index                   = 0;
        vol.origin                  = { 0.0f, 5.0f, 0.0f };       // Center at (0, 5, 0)
        vol.eulerAngles             = { 0.0f, 0.0f, 0.0f };
        vol.probeSpacing            = { 1.5f, 1.5f, 1.5f };

        // 20×10×20 / 1.5 ≈ 14 × 8 × 14 probes
        vol.probeCounts             = {
            static_cast<int>(ceilf(20.0f / 1.5f)),
            static_cast<int>(ceilf(10.0f / 1.5f)),
            static_cast<int>(ceilf(20.0f / 1.5f))
        };

        // Probe configuration (texture atlas dimensions)
        vol.probeNumRays                        = 256;
        vol.probeNumIrradianceInteriorTexels    = 6;    // 6×6 interior + border = 8×8
        vol.probeNumDistanceInteriorTexels      = 14;   // 14×14 interior + border = 16×16
        vol.probeNumIrradianceTexels            = vol.probeNumIrradianceInteriorTexels + 2;
        vol.probeNumDistanceTexels              = vol.probeNumDistanceInteriorTexels + 2;

        // Texture formats
        vol.probeRayDataFormat      = rtxgi::EDDGIVolumeTextureFormat::F32x2;
        vol.probeIrradianceFormat   = rtxgi::EDDGIVolumeTextureFormat::U32;    // R10G10B10A2
        vol.probeDistanceFormat     = rtxgi::EDDGIVolumeTextureFormat::F16x2;  // RG16
        vol.probeDataFormat         = rtxgi::EDDGIVolumeTextureFormat::F16x4;  // RGBA16

        // Compute texture dimensions for each type
        UINT texW, texH, texArraySize;
        rtxgi::GetDDGIVolumeTextureDimensions(vol, rtxgi::EDDGIVolumeTextureType::Irradiance,
                                               texW, texH, texArraySize);
        m_IrradianceTexDim  = Vector3U{ texW, texH, texArraySize };

        rtxgi::GetDDGIVolumeTextureDimensions(vol, rtxgi::EDDGIVolumeTextureType::Distance,
                                               texW, texH, texArraySize);
        m_DistanceTexDim    = Vector3U{ texW, texH, texArraySize };

        rtxgi::GetDDGIVolumeTextureDimensions(vol, rtxgi::EDDGIVolumeTextureType::Data,
                                               texW, texH, texArraySize);
        m_ProbeDataTexDim   = Vector3U{ texW, texH, texArraySize };

        // Compute RayData texture dimensions: (numRays × totalProbes, 1, 1) stored as 2D
        const UINT totalProbes = static_cast<UINT>(vol.probeCounts.x * vol.probeCounts.y * vol.probeCounts.z);
        UINT rayDataW, rayDataH, rayDataArraySize;
        rtxgi::GetDDGIVolumeTextureDimensions(vol, rtxgi::EDDGIVolumeTextureType::RayData,
                                               rayDataW, rayDataH, rayDataArraySize);
        m_RayDataTexDim     = Vector3U{ rayDataW, rayDataH, rayDataArraySize };

        nvrhi::DeviceHandle device = g_Renderer.m_RHI->m_NvrhiDevice;

        // Irradiance: R10G10B10A2_UNORM, 2D Array, UAV
        {
            nvrhi::TextureDesc desc;
            desc.width      = m_IrradianceTexDim.x;
            desc.height     = m_IrradianceTexDim.y;
            desc.arraySize  = m_IrradianceTexDim.z;
            desc.dimension  = nvrhi::TextureDimension::Texture2DArray;
            desc.format     = nvrhi::Format::R10G10B10A2_UNORM;
            desc.isUAV      = true;
            desc.debugName  = "DDGI Irradiance";
            desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
            desc.keepInitialState = true;
            m_IrradianceTexture = device->createTexture(desc);
        }

        // Distance: RG16_FLOAT, 2D Array, UAV
        {
            nvrhi::TextureDesc desc;
            desc.width      = m_DistanceTexDim.x;
            desc.height     = m_DistanceTexDim.y;
            desc.arraySize  = m_DistanceTexDim.z;
            desc.format     = nvrhi::Format::RG16_FLOAT;
            desc.dimension  = nvrhi::TextureDimension::Texture2DArray;
            desc.isUAV      = true;
            desc.debugName  = "DDGI Distance";
            desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
            desc.keepInitialState = true;
            m_DistanceTexture = device->createTexture(desc);
        }

        // Probe Data: RGBA16_FLOAT, 2D Array, UAV
        {
            nvrhi::TextureDesc desc;
            desc.width      = m_ProbeDataTexDim.x;
            desc.height     = m_ProbeDataTexDim.y;
            desc.arraySize  = m_ProbeDataTexDim.z;
            desc.format     = nvrhi::Format::RGBA16_FLOAT;
            desc.dimension  = nvrhi::TextureDimension::Texture2DArray;
            desc.isUAV      = true;
            desc.debugName  = "DDGI Probe Data";
            desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
            desc.keepInitialState = true;
            m_ProbeDataTexture = device->createTexture(desc);
        }

        // RayData: RG32_FLOAT, 2D Array, UAV
        // F32x2 format: .x = packed radiance, .y = hitT
        {
            nvrhi::TextureDesc desc;
            desc.width      = m_RayDataTexDim.x;
            desc.height     = m_RayDataTexDim.y;
            desc.arraySize  = m_RayDataTexDim.z;
            desc.format     = nvrhi::Format::RG32_FLOAT;
            desc.dimension  = nvrhi::TextureDimension::Texture2DArray;
            desc.isUAV      = true;
            desc.debugName  = "DDGI Ray Data";
            desc.initialState     = nvrhi::ResourceStates::UnorderedAccess;
            desc.keepInitialState = true;
            m_RayDataTexture = device->createTexture(desc);
        }

        // DDGIVolumes structured buffer — one packed GPU descriptor per volume,
        // written each frame from the updated CPU-side descriptor.
        {
            nvrhi::BufferDesc desc;
            desc.byteSize           = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
            desc.structStride       = sizeof(rtxgi::DDGIVolumeDescGPUPacked);
            desc.debugName          = "DDGIVolumesBuffer";
            desc.initialState       = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState   = true;
            m_DDGIVolumesBuffer = device->createBuffer(desc);
        }

        // Register persistent textures in the global bindless heap
        g_Renderer.RegisterTexture(m_IrradianceTexture);
        g_Renderer.RegisterTexture(m_DistanceTexture);
        g_Renderer.RegisterTexture(m_ProbeDataTexture);

        // Tie the CPU-side descriptor and GPU textures together, then hand ownership to the scene.
        DDGIVolumeNvrhi volume;
        volume.InitFromDesc(vol);
        volume.m_IrradianceTexture = m_IrradianceTexture;
        volume.m_DistanceTexture   = m_DistanceTexture;
        volume.m_ProbeDataTexture  = m_ProbeDataTexture;
        volume.Update(); // computes initial rotation matrices/quaternions
        const uint32_t gpuMemoryBytes = volume.GetGPUMemoryUsedInBytes();
        g_Renderer.m_Scene.m_DDGIVolumes.push_back(volume);

        // Log volume info
        UINT probeCountX, probeCountY, probeCountZ;
        rtxgi::GetDDGIVolumeProbeCounts(vol, probeCountX, probeCountY, probeCountZ);
        SDL_Log("[DDGI] Volume created: %dx%dx%d probes, spacing %.1fm, origin (%.1f, %.1f, %.1f)",
                probeCountX, probeCountY, probeCountZ,
                vol.probeSpacing.x,
                vol.origin.x, vol.origin.y, vol.origin.z);
        SDL_Log("[DDGI]   Irradiance: %ux%ux%u (R10G10B10A2), Distance: %ux%ux%u (RG16), Data: %ux%ux%u (RGBA16)",
                m_IrradianceTexDim.x, m_IrradianceTexDim.y, m_IrradianceTexDim.z,
                m_DistanceTexDim.x, m_DistanceTexDim.y, m_DistanceTexDim.z,
                m_ProbeDataTexDim.x, m_ProbeDataTexDim.y, m_ProbeDataTexDim.z);
        SDL_Log("[DDGI]   RayData: %ux%ux%u (RG32F, %u probes × %u rays)",
                m_RayDataTexDim.x, m_RayDataTexDim.y, m_RayDataTexDim.z,
                totalProbes, vol.probeNumRays);
        SDL_Log("[DDGI]   GPU memory: %.2f MB", BYTES_TO_MB(gpuMemoryBytes));
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Renderer.m_EnableDDGIProbeTracing)
            return false;

        // Declare depth read (needed by probe overlay debug mode 2)
        renderGraph.ReadTexture(g_RG_DepthTexture);

        // Declare the per-frame overlay output (transient).
        // Always declared so DeferredRenderer can read it regardless of debug mode.
        {
            auto [width, height] = g_Renderer.SwapchainSize();
            RGTextureDesc desc;
            desc.m_NvrhiDesc.width         = width;
            desc.m_NvrhiDesc.height        = height;
            desc.m_NvrhiDesc.format        = nvrhi::Format::RGBA16_FLOAT;
            desc.m_NvrhiDesc.isUAV         = true;
            desc.m_NvrhiDesc.debugName     = "DDGIProbeOverlay";
            desc.m_NvrhiDesc.initialState  = nvrhi::ResourceStates::UnorderedAccess;
            desc.m_NvrhiDesc.keepInitialState = true;
            renderGraph.DeclareTexture(desc, g_RG_DDGIDebugOverlay);
        }

        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        PROFILE_FUNCTION();

        nvrhi::DeviceHandle device = g_Renderer.m_RHI->m_NvrhiDevice;

        if (g_Renderer.m_Scene.m_DDGIVolumes.empty())
            return;

        // ── 0. Update volume rotation matrices for this frame ──────────────
        for (DDGIVolumeNvrhi& volume : g_Renderer.m_Scene.m_DDGIVolumes)
            volume.Update();

        // ── 1. Upload packed GPU descriptor for volume 0 ──────────────────
        {
            DDGIVolumeNvrhi& volume = g_Renderer.m_Scene.m_DDGIVolumes[0];
            rtxgi::DDGIVolumeDescGPUPacked packed = volume.GetDescGPUPacked();
            commandList->writeBuffer(m_DDGIVolumesBuffer, &packed, sizeof(packed), 0);
        }

        // ── 2. Probe trace dispatch ────────────────────────────────────────
        {
            DDGIVolumeNvrhi& volume = g_Renderer.m_Scene.m_DDGIVolumes[0];
            rtxgi::DDGIVolumeDesc desc = volume.GetDesc();

            const uint32_t totalProbes = static_cast<uint32_t>(
                desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z);
            const uint32_t totalRays   = totalProbes * static_cast<uint32_t>(desc.probeNumRays);
            const uint32_t groupsX     = (totalRays + 63u) / 64u;

            // Build constant buffer
            const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
                sizeof(srrhi::ProbeTraceConstants), "ProbeTraceCB", 1);
            nvrhi::BufferHandle cb = device->createBuffer(cbDesc);

            srrhi::ProbeTraceConstants constants;
            constants.SetSunDirection(g_Renderer.m_Scene.GetSunDirection());
            constants.SetSunIntensity(g_Renderer.m_EnableSky ? g_Renderer.m_Scene.GetSunIntensity() : 0.0f);
            constants.SetFrame(g_Renderer.m_FrameNumber);
            constants.SetVolumeIndex(0);
            constants.SetLightCount(g_Renderer.m_Scene.m_LightCount);
            commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

            // Build binding set
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
            inputs.SetProbeData(m_ProbeDataTexture);
            inputs.SetRayData(m_RayDataTexture, 0, 0, -1);

            nvrhi::BindingSetDesc bset = Renderer::CreateBindingSetDesc(inputs);

            Renderer::RenderPassParams params;
            params.commandList    = commandList;
            params.shaderID       = ShaderID::DDGI_PROBETRACECS_PROBETRACECS;
            params.bindingSetDesc = bset;
            params.dispatchParams = { groupsX, 1, 1 };
            g_Renderer.AddComputePass(params);
        }

        // ── 3. Probe Overlay debug pass (mode 2) ───────────────────────────
        {
            nvrhi::TextureHandle overlay = renderGraph.GetTexture(g_RG_DDGIDebugOverlay, RGResourceAccessMode::Write);

            // Always clear to transparent black so that blending in DeferredLighting is a no-op
            // when the debug mode is off.
            commandList->clearTextureFloat(overlay, nvrhi::AllSubresources, nvrhi::Color{ 0.0f });

            if (g_Renderer.m_DDGIDebugMode == srrhi::DDGIDebugMode::DDGI_DEBUG_PROBE_POSITIONS)
            {
                DDGIVolumeNvrhi& volume = g_Renderer.m_Scene.m_DDGIVolumes[0];
                rtxgi::DDGIVolumeDesc desc = volume.GetDesc();

                const uint32_t totalProbes = static_cast<uint32_t>(
                    desc.probeCounts.x * desc.probeCounts.y * desc.probeCounts.z);
                const uint32_t groupsX = (totalProbes + 63u) / 64u;

                auto [width, height] = g_Renderer.SwapchainSize();

                const nvrhi::BufferDesc cbDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
                    sizeof(srrhi::ProbeOverlayConstants), "ProbeOverlayCB", 1);
                nvrhi::BufferHandle cb = device->createBuffer(cbDesc);

                srrhi::ProbeOverlayConstants constants;
                constants.SetMatWorldToClip(g_Renderer.m_Scene.m_View.m_MatWorldToClip);
                constants.SetViewportSize({ static_cast<float>(width), static_cast<float>(height) });
                constants.SetViewportSizeInv({ 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height) });
                constants.SetVolumeIndex(0);
                commandList->writeBuffer(cb, &constants, sizeof(constants), 0);

                nvrhi::TextureHandle depth = renderGraph.GetTexture(g_RG_DepthTexture, RGResourceAccessMode::Read);

                srrhi::ProbeOverlayInputs inputs;
                inputs.SetProbeOverlayCB(cb);
                inputs.SetDDGIVolumes(m_DDGIVolumesBuffer);
                inputs.SetProbeData(m_ProbeDataTexture);
                inputs.SetDepth(depth);
                inputs.SetOverlayOutput(overlay, 0);

                nvrhi::BindingSetDesc bset = Renderer::CreateBindingSetDesc(inputs);

                Renderer::RenderPassParams params;
                params.commandList    = commandList;
                params.shaderID       = ShaderID::DDGI_PROBEOVERLAYCS_PROBEOVERLAYCS;
                params.bindingSetDesc = bset;
                params.dispatchParams = { groupsX, 1, 1 };
                g_Renderer.AddComputePass(params);
            }
        }
    }

    const char* GetName() const override { return "DDGIRenderer"; }

private:
    Vector3U m_IrradianceTexDim = {};
    Vector3U m_DistanceTexDim   = {};
    Vector3U m_ProbeDataTexDim  = {};
    Vector3U m_RayDataTexDim    = {};

    nvrhi::TextureHandle m_IrradianceTexture;
    nvrhi::TextureHandle m_DistanceTexture;
    nvrhi::TextureHandle m_ProbeDataTexture;
    nvrhi::TextureHandle m_RayDataTexture;

    nvrhi::BufferHandle  m_DDGIVolumesBuffer;   // Structured buffer of packed DDGIVolumeDescGPU
};

REGISTER_RENDERER(DDGIRenderer)
