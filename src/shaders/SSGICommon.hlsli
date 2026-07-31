#ifndef SSGI_COMMON_HLSLI
#define SSGI_COMMON_HLSLI

#include "Common.hlsli"
#include "CommonLighting.hlsli"

// Screen-space direct lighting at `uv`: the outgoing (Lambertian) radiance the surface
// visible at that texel emits because of the sun, plus its emissive.
//
// Without it the ray march can only return the previous frame's GI accumulation at a hit, which makes zero
// a fixed point of the feedback loop — the accumulation starts black and stays black.
//
// Lambertian only on purpose: the hit is a secondary bounce, so the view-dependent
// specular lobe at the hit point is not meaningful for the shading pixel.
float3 SSGIScreenDirectLight(Texture2D<float4> albedoTex, Texture2D<float2> normalTex, Texture2D<float2> ormTex,
                             Texture2D<float4> emissiveTex, Texture2D<float> shadowMaskTex, SamplerState samp,
                             float2 uv, float3 sunDirection, float3 sunRadiance)
{
    float3 albedo    = albedoTex.SampleLevel(samp, uv, 0.0f).rgb;
    float3 normal    = DecodeNormal(normalTex.SampleLevel(samp, uv, 0.0f));
    float  metalness = ormTex.SampleLevel(samp, uv, 0.0f).g;
    float  shadow    = shadowMaskTex.SampleLevel(samp, uv, 0.0f);
    float3 emissive  = emissiveTex.SampleLevel(samp, uv, 0.0f).rgb;

    float  NoL           = saturate(dot(normal, sunDirection));
    float3 diffuseAlbedo = albedo * (1.0f - metalness);

    return diffuseAlbedo * (NoL * shadow / srrhi::CommonConsts::PI) * sunRadiance + emissive;
}

#endif // SSGI_COMMON_HLSLI
