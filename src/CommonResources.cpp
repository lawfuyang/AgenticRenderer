#include "pch.h"
#include "CommonResources.h"
#include "Renderer.h"

bool CommonResources::Initialize()
{
    Renderer* renderer = Renderer::GetInstance();
    nvrhi::IDevice* device = renderer->m_NvrhiDevice;

    // Helper lambda to create samplers with error checking
    auto createSampler = [device](const char* name, bool linearFilter, nvrhi::SamplerAddressMode addressMode, float anisotropy = 1.0f) -> nvrhi::SamplerHandle
    {
        nvrhi::SamplerDesc desc;
        desc.setAllFilters(linearFilter);
        desc.setAllAddressModes(addressMode);
        if (anisotropy > 1.0f)
            desc.setMaxAnisotropy(anisotropy);
        
        nvrhi::SamplerHandle sampler = device->createSampler(desc);
        if (!sampler)
        {
            SDL_Log("[CommonResources] Failed to create %s sampler", name);
            SDL_assert(false && "Failed to create common sampler");
        }
        return sampler;
    };

    // Create common samplers (all must succeed)
    LinearClamp = createSampler("LinearClamp", true, nvrhi::SamplerAddressMode::ClampToEdge);
    LinearWrap = createSampler("LinearWrap", true, nvrhi::SamplerAddressMode::Wrap);
    PointClamp = createSampler("PointClamp", false, nvrhi::SamplerAddressMode::ClampToEdge);
    PointWrap = createSampler("PointWrap", false, nvrhi::SamplerAddressMode::Wrap);
    Anisotropic = createSampler("Anisotropic", true, nvrhi::SamplerAddressMode::Wrap, 16.0f);

    SDL_Log("[CommonResources] Initialized successfully");
    return true;
}

void CommonResources::Shutdown()
{
    Anisotropic = nullptr;
    PointWrap = nullptr;
    PointClamp = nullptr;
    LinearWrap = nullptr;
    LinearClamp = nullptr;
}
