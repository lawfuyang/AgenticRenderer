#ifndef BINDLESS_HLSLI
#define BINDLESS_HLSLI

#include "ShaderShared.h"

// Scene buffers and samplers in space1
// These matches the layout in BasePass.hlsl and are now used in DeferredLighting.hlsl for RT shadows.

// Helper function to sample from bindless textures using a sampler index.
// Used in Pixel Shaders where implicit derivatives are available for mipmapping/anisotropy.
float4 SampleBindlessTexture(uint textureIndex, SamplerState clampSampler, SamplerState wrapSampler, uint samplerIndex, float2 uv)
{
    Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    
    // Sample once using branch-based selection to avoid sampler-typed phi nodes.
    if (samplerIndex == SAMPLER_CLAMP_INDEX)
        return tex.Sample(clampSampler, uv);
    else
        return tex.Sample(wrapSampler, uv);
}

// Helper function to sample from bindless textures at a specific LOD level.
// Required in Compute Shaders or Ray Tracing where implicit derivatives are unavailable.
float4 SampleBindlessTextureLevel(uint textureIndex, SamplerState clampSampler, SamplerState wrapSampler, uint samplerIndex, float2 uv, float lod)
{
    Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    
    if (samplerIndex == SAMPLER_CLAMP_INDEX)
        return tex.SampleLevel(clampSampler, uv, lod);
    else
        return tex.SampleLevel(wrapSampler, uv, lod);
}

// Helper function to sample from bindless textures with explicit gradients.
// Useful for ray tracing where implicit derivatives are unavailable.
float4 SampleBindlessTextureGrad(uint textureIndex, SamplerState clampSampler, SamplerState wrapSampler, uint samplerIndex, float2 uv, float2 ddx_uv, float2 ddy_uv)
{
    Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    
    if (samplerIndex == SAMPLER_CLAMP_INDEX)
        return tex.SampleGrad(clampSampler, uv, ddx_uv, ddy_uv);
    else
        return tex.SampleGrad(wrapSampler, uv, ddx_uv, ddy_uv);
}

#endif // BINDLESS_HLSLI
