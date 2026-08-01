#include "Renderer.h"
#include "CommonResources.h"

#include <rtxgi/ddgi/DDGIVolume.h>
#include <rtxgi/ddgi/gfx/DDGIVolume_D3D12.h>
#include <imgui.h>

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
        rtxgi::DDGIVolumeDesc& vol = g_Renderer.m_Scene.m_DDGIVolume;
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

        // Allocate persistent textures via nvrhi
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

        // Register textures in the global bindless heap
        g_Renderer.RegisterTexture(m_IrradianceTexture);
        g_Renderer.RegisterTexture(m_DistanceTexture);
        g_Renderer.RegisterTexture(m_ProbeDataTexture);

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
    }

    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Renderer.m_EnableDDGIProbeTracing)
            return false;
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
    }

    const char* GetName() const override { return "DDGIRenderer"; }

private:
    Vector3U m_IrradianceTexDim = {};
    Vector3U m_DistanceTexDim   = {};
    Vector3U m_ProbeDataTexDim  = {};

    nvrhi::TextureHandle m_IrradianceTexture;
    nvrhi::TextureHandle m_DistanceTexture;
    nvrhi::TextureHandle m_ProbeDataTexture;
};

REGISTER_RENDERER(DDGIRenderer)
