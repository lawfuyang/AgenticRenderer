// DDGI Probe Visualization — RayQuery compute shader.
// Follows the RTXGI reference sample (ProbesRGS.hlsl), adapted to inline RayQuery.
//
// Debug modes (g_CB.m_DebugMode, from srrhi::DDGIDebugMode):
//   DDGI_DEBUG_PROBE_POSITIONS     — green=active, red=inactive per-probe state
//   DDGI_DEBUG_PROBE_IRRADIANCE    — spherical irradiance look-up (gamma-decoded)
//   DDGI_DEBUG_PROBE_DISTANCE      — distance heatmap (blue=near → red=far)
//   DDGI_DEBUG_PROBE_CLASSIFICATION — same as positions but yellow for transitional state

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"

#include "../Common.hlsli"           // MatrixMultiply, ReconstructWorldPos
#include "../CommonLighting.hlsli"
#include "../Bindless.hlsli"
#include "../srrhi/hlsl/ProbeVis.hlsli"

static const srrhi::ProbeVisConstants                   g_CB        = srrhi::ProbeVisInputs::GetProbeVisCB();
static const RaytracingAccelerationStructure             g_ProbeTLAS = srrhi::ProbeVisInputs::GetProbeTLAS();
static const StructuredBuffer<DDGIVolumeDescGPUPacked>   g_Volumes   = srrhi::ProbeVisInputs::GetDDGIVolumes();
static const Texture2D<float>                            g_Depth     = srrhi::ProbeVisInputs::GetDepth();
static       RWTexture2D<float4>                         g_Output    = srrhi::ProbeVisInputs::GetOverlayOutput();

// ── Helpers ────────────────────────────────────────────────────────────────

RayDesc BuildCameraRay(uint2 px)
{
    // Camera position: (0,0,0,1) in view-space → world-space.
    // MatrixMultiply(row, matrix) = row * M; with row_major this extracts row w-components.
    float4 pos4  = MatrixMultiply(float4(0, 0, 0, 1), g_CB.m_MatViewToWorld);
    float3 pos   = pos4.xyz;

    // Compute a point on the far clip plane, transform to world, then shoot the ray.
    float2 uv     = (float2(px) + 0.5f) * g_CB.m_ViewportSizeInv;
    float2 clipXY = UVToClipXY(uv);
    float4 farWorld  = MatrixMultiply(float4(clipXY, 0.9f, 1.0f), g_CB.m_MatClipToWorld);
    float3 farPoint  = farWorld.xyz / farWorld.w;

    RayDesc ray;
    ray.Origin    = pos;
    ray.Direction = normalize(farPoint - pos);
    ray.TMin      = 0.001f;
    ray.TMax      = 1.0e8f;
    return ray;
}

// Wrap Texture2DArray + SamplerDescriptorHeap in a file-scope helper
// (the same proven pattern as SampleBindlessTextureLevel in Bindless.hlsli).
float4 SampleDDGIAtlas(uint texIndex, uint samplerIndex, float3 texUV, float lod)
{
    Texture2DArray<float4> tex = ResourceDescriptorHeap[NonUniformResourceIndex(texIndex)];
    SamplerState           sam = SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
    return tex.SampleLevel(sam, texUV, lod);
}

// ── Entry point ────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void ProbeVisCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 px = dispatchThreadID.xy;
    if (any(px >= (uint2)g_CB.m_ViewportSize))
        return;

    RayDesc ray = BuildCameraRay(px);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(g_ProbeTLAS, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        g_Output[px] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // ── Depth test: cull probes behind scene geometry ──────────────────
    float  sceneDepth = g_Depth.Load(int3(px, 0));
    float3 hitPos     = ray.Origin + ray.Direction * q.CommittedRayT();

    float4 hitClip   = MatrixMultiply(float4(hitPos, 1.0f), g_CB.m_MatWorldToClip);
    float  probeNdcZ = hitClip.z / max(hitClip.w, 1e-8f);

    // Reversed-Z: near=1 (closest), far≈0 (sky/far-plane). Treat tiny depth
    // the same as sky to avoid precision issues at the far plane.
    const float kFarThreshold = 1e-4f;
    if (sceneDepth > kFarThreshold && probeNdcZ < sceneDepth - 1e-5f)
    {
        g_Output[px] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // ── Decode instance ID → volume + probe index ────────────────────────
    uint instID    = q.CommittedInstanceID();
    uint volIdx    = (instID >> 16) & 0xFF;
    uint probeIdx  = instID & 0xFFFF;

    DDGIVolumeDescGPU volume      = UnpackDDGIVolumeDescGPU(g_Volumes[volIdx]);
    int3              probeCoords = DDGIGetProbeCoords((int)probeIdx, volume);
    int               scrolledIdx = DDGIGetScrollingProbeIndex(probeCoords, volume);

    float3 shift    = volume.probeSpacing.xyz * (volume.probeCounts.xyz - 1) * 0.5f;
    float3 worldPos = float3(
        volume.origin.x + (float)probeCoords.x * volume.probeSpacing.x - shift.x,
        volume.origin.y + (float)probeCoords.y * volume.probeSpacing.y - shift.y,
        volume.origin.z + (float)probeCoords.z * volume.probeSpacing.z - shift.z);

    float3 worldNormal = normalize(hitPos - worldPos);

    // ── Debug visualisation ──────────────────────────────────────────────
    uint   visType = g_CB.m_DebugMode;
    float3 color   = float3(0.25f, 0.25f, 0.25f);

    if (visType == srrhi::DDGIDebugMode::DDGI_DEBUG_PROBE_IRRADIANCE)
    {
        float2 octUV = DDGIGetOctahedralCoordinates(worldNormal);
        float3 texUV = DDGIGetProbeUV(scrolledIdx, octUV, volume.probeNumIrradianceInteriorTexels, volume);
        float3 raw   = SampleDDGIAtlas(g_CB.m_IrradianceTexIndex,
                                       srrhi::CommonConsts::SAMPLER_LINEAR_CLAMP_INDEX,
                                       texUV, 0).rgb;

        float3 exponent = volume.probeIrradianceEncodingGamma * 0.5f;
        color = pow(raw, exponent);
        color *= color * (2.0f * 3.14159265f);  // RTXGI_2PI (integration-domain area)
        if (volume.probeIrradianceFormat == RTXGI_DDGI_VOLUME_TEXTURE_FORMAT_U32)
            color *= 0.5f;  // SDK: U32 stores 2× irradiance for higher precision
    }
    else if (visType == srrhi::DDGIDebugMode::DDGI_DEBUG_PROBE_DISTANCE)
    {
        float2 octUV = DDGIGetOctahedralCoordinates(worldNormal);
        float3 texUV = DDGIGetProbeUV(scrolledIdx, octUV, volume.probeNumDistanceInteriorTexels, volume);
        float d = 2.0f * SampleDDGIAtlas(g_CB.m_DistanceTexIndex,
                                          srrhi::CommonConsts::SAMPLER_LINEAR_CLAMP_INDEX,
                                          texUV, 0).r;

        float v = saturate(d / 10.0f);
        color = lerp(float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), v);
    }
    else  // DDGI_DEBUG_PROBE_POSITIONS  or  DDGI_DEBUG_PROBE_CLASSIFICATION
    {
        float3 texUV = DDGIGetProbeUV(scrolledIdx, float2(0.5f, 0.5f), 1, volume);
        float  state  = SampleDDGIAtlas(g_CB.m_ProbeDataTexIndex,
                                         srrhi::CommonConsts::SAMPLER_POINT_CLAMP_INDEX,
                                         texUV, 0).w;

        if (state == RTXGI_DDGI_PROBE_STATE_ACTIVE)
            color = float3(0.0f, 1.0f, 0.0f);
        else if (state == RTXGI_DDGI_PROBE_STATE_INACTIVE)
            color = float3(1.0f, 0.0f, 0.0f);
        else
            color = float3(1.0f, 1.0f, 0.0f);
    }

    float lum = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = color / (1.0f + lum);
    color = pow(color, 1.0f / 2.2f);

    g_Output[px] = float4(color, 1.0f);
}
