// DDGI Indirect Query — fullscreen compute shader.
// One thread per pixel, 8x8 thread groups.
// Reconstructs world position from depth, decodes the world-space normal,
// then loops over all DDGI volumes to accumulate irradiance.
// Output: g_RG_DDGIIndirect (RGBA16_FLOAT, screen resolution).

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"
#include "ddgi/Irradiance.hlsl"

#include "../Common.hlsli"
#include "../CommonLighting.hlsli"
#include "../Bindless.hlsli"
#include "../srrhi/hlsl/IndirectQuery.hlsli"

static const srrhi::IndirectQueryConstants                    g_CB         = srrhi::IndirectQueryInputs::GetIndirectQueryCB();
static const StructuredBuffer<DDGIVolumeDescGPUPacked>        g_Volumes    = srrhi::IndirectQueryInputs::GetDDGIVolumes();
static const StructuredBuffer<DDGIVolumeResourceIndices>      g_ResIndices = srrhi::IndirectQueryInputs::GetDDGIResourceIndices();
static const Texture2D<float>                                 g_Depth      = srrhi::IndirectQueryInputs::GetDepth();
static const Texture2D<float2>                                g_Normals    = srrhi::IndirectQueryInputs::GetNormals();
static       RWTexture2D<float4>                              g_Output     = srrhi::IndirectQueryInputs::GetOutput();

[numthreads(8, 8, 1)]
void IndirectQueryCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 px = dispatchThreadID.xy;
    if (any(px >= (uint2)g_CB.m_View.m_ViewportSize))
        return;

    // ---- Reconstruct world position ------------------------------------------
    float  depth = g_Depth.Load(int3(px, 0));

    // Reversed-Z: DEPTH_FAR (0.0) means no geometry at this pixel.
    if (depth <= srrhi::CommonConsts::DEPTH_FAR + 1e-5f)
    {
        g_Output[px] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float2 uv       = (float2(px) + 0.5f) * g_CB.m_View.m_ViewportSizeInv;
    float3 worldPos = ReconstructWorldPos(uv, depth, g_CB.m_View.m_MatClipToWorld);

    // ---- Decode world-space normal ------------------------------------------
    float3 worldNormal = DecodeNormal(g_Normals.Load(int3(px, 0)));

    // ---- Accumulate irradiance across all volumes ---------------------------
    float3 accumulatedIrradiance = float3(0.0f, 0.0f, 0.0f);
    float  totalWeight           = 0.0f;

    for (uint volIdx = 0; volIdx < g_CB.m_NumVolumes; ++volIdx)
    {
        DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_Volumes[volIdx]);

        float blendWeight = DDGIGetVolumeBlendWeight(worldPos, volume);
        if (blendWeight <= 0.0f)
            continue;

        // Build DDGIVolumeResources from bindless heap indices
        DDGIVolumeResources res;
        res.probeIrradiance = ResourceDescriptorHeap[NonUniformResourceIndex(g_ResIndices[volIdx].probeIrradianceSRVIndex)];
        res.probeDistance   = ResourceDescriptorHeap[NonUniformResourceIndex(g_ResIndices[volIdx].probeDistanceSRVIndex)];
        res.probeData       = ResourceDescriptorHeap[NonUniformResourceIndex(g_ResIndices[volIdx].probeDataSRVIndex)];
        res.bilinearSampler = SamplerDescriptorHeap[NonUniformResourceIndex(srrhi::CommonConsts::SAMPLER_LINEAR_CLAMP_INDEX)];

        float3 cameraDir   = normalize(g_CB.m_View.m_CameraDirectionOrPosition.xyz - worldPos);
        float3 surfaceBias = DDGIGetSurfaceBias(worldNormal, cameraDir, volume);

        // Diffuse irradiance lookup; direction = surface normal.
        float3 irradiance = DDGIGetVolumeIrradiance(worldPos, surfaceBias, worldNormal, volume, res);

        accumulatedIrradiance += irradiance * blendWeight;
        totalWeight           += blendWeight;
    }

    if (totalWeight > 0.0f)
        accumulatedIrradiance /= totalWeight;

    g_Output[px] = float4(accumulatedIrradiance * g_CB.m_IndirectIntensity, 1.0f);
}
