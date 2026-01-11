#pragma once

#include "pch.h"

// Common GPU resources shared across the application (samplers, default textures, etc.)
class CommonResources
{
public:
    static CommonResources& GetInstance()
    {
        static CommonResources instance;
        return instance;
    }

    // Delete copy and move constructors/operators
    CommonResources(const CommonResources&) = delete;
    CommonResources& operator=(const CommonResources&) = delete;
    CommonResources(CommonResources&&) = delete;
    CommonResources& operator=(CommonResources&&) = delete;

    bool Initialize();
    void Shutdown();

    // Common sampler states
    nvrhi::SamplerHandle LinearClamp;   // Bilinear filtering, clamp to edge
    nvrhi::SamplerHandle LinearWrap;    // Bilinear filtering, wrap/repeat
    nvrhi::SamplerHandle PointClamp;    // Point/nearest filtering, clamp to edge
    nvrhi::SamplerHandle PointWrap;     // Point/nearest filtering, wrap/repeat
    nvrhi::SamplerHandle Anisotropic;   // Anisotropic filtering, wrap

private:
    CommonResources() = default;
};
