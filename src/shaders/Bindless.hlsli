#ifndef BINDLESS_HLSLI
#define BINDLESS_HLSLI

#include "ShaderShared.h"

// Scene buffers and samplers in space1
// These matches the layout in BasePass.hlsl and are now used in DeferredLighting.hlsl for RT shadows.

#ifndef BINDLESS_RESOURCES_DEFINED
#define BINDLESS_RESOURCES_DEFINED

// Note: These are in space1
SamplerState g_SamplerAnisoClamp : register(s0, space1);
SamplerState g_SamplerAnisoWrap  : register(s1, space1);

#endif

// Helper function to sample from bindless textures using a sampler index.
// Used in Pixel Shaders where implicit derivatives are available for mipmapping/anisotropy.
float4 SampleBindlessTexture(uint textureIndex, uint samplerIndex, float2 uv)
{
    Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    
    // Sample once using branch-based selection to avoid sampler-typed phi nodes.
    if (samplerIndex == SAMPLER_CLAMP_INDEX)
        return tex.Sample(g_SamplerAnisoClamp, uv);
    else
        return tex.Sample(g_SamplerAnisoWrap, uv);
}

// Helper function to sample from bindless textures at a specific LOD level.
// Required in Compute Shaders or Ray Tracing where implicit derivatives are unavailable.
float4 SampleBindlessTextureLevel(uint textureIndex, uint samplerIndex, float2 uv, float lod)
{
    Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    
    if (samplerIndex == SAMPLER_CLAMP_INDEX)
        return tex.SampleLevel(g_SamplerAnisoClamp, uv, lod);
    else
        return tex.SampleLevel(g_SamplerAnisoWrap, uv, lod);
}

#endif // BINDLESS_HLSLI
