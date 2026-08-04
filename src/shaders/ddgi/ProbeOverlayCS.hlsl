// DDGI Probe Overlay Compute Shader
// Debug mode 2 (DDGI_DEBUG_PROBE_POSITIONS): one thread per probe,
// projects probe world position to screen, depth-tests against the depth buffer,
// and draws a 3×3 dot. Color: green for active probes, red for inactive probes.

#include "DDGIShaderConfig.h"

#include "ProbeCommon.hlsl"

#include "../srrhi/hlsl/ProbeOverlay.hlsli"
#include "../CommonLighting.hlsli"   // for MatrixMultiply

static const srrhi::ProbeOverlayConstants                   g_OverlayCB = srrhi::ProbeOverlayInputs::GetProbeOverlayCB();
static const StructuredBuffer<DDGIVolumeDescGPUPacked>      g_Volumes   = srrhi::ProbeOverlayInputs::GetDDGIVolumes();
static const Texture2DArray<float4>                         g_ProbeData = srrhi::ProbeOverlayInputs::GetProbeData();
static const Texture2D<float>                               g_Depth     = srrhi::ProbeOverlayInputs::GetDepth();
static       RWTexture2D<float4>                            g_Overlay   = srrhi::ProbeOverlayInputs::GetOverlayOutput();

[numthreads(64, 1, 1)]
void ProbeOverlayCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_Volumes[g_OverlayCB.m_VolumeIndex]);

    int totalProbes = volume.probeCounts.x * volume.probeCounts.y * volume.probeCounts.z;
    int probeIndex  = (int)dispatchThreadID.x;
    if (probeIndex >= totalProbes)
        return;

    // Probe world position (including relocation offset)
    int3 probeCoords        = DDGIGetProbeCoords(probeIndex, volume);
    int  scrolledProbeIndex = DDGIGetScrollingProbeIndex(probeCoords, volume);
    float3 worldPos         = DDGIGetProbeWorldPosition(probeCoords, volume, g_ProbeData);

    // Project to clip space
    float4 clip = MatrixMultiply(float4(worldPos, 1.0f), g_OverlayCB.m_MatWorldToClip);

    // Clip-space behind the camera or outside the near plane
    if (clip.w <= 0.0f)
        return;

    float3 ndc = clip.xyz / clip.w;

    // Outside the frustum
    if (any(abs(ndc.xy) > 1.0f))
        return;

    // Convert NDC → pixel coords (origin top-left, y-down)
    float2 uv     = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    int2   center = int2(uv * g_OverlayCB.m_ViewportSize);

    // Depth test: compare probe NDC depth against depth buffer (reversed-Z: near=1, far=0)
    float probeNDCDepth = ndc.z; // [0,1] in DX clip space
    float sceneDepth    = g_Depth.Load(int3(center, 0));
    // Reversed-Z: probeNDCDepth must be >= sceneDepth (i.e. not behind geometry)
    if (probeNDCDepth < sceneDepth - 1e-4f)
        return;

    // Classification color: green = active, red = inactive
    float probeState = DDGILoadProbeState(scrolledProbeIndex, g_ProbeData, volume);
    float4 color = (probeState == RTXGI_DDGI_PROBE_STATE_ACTIVE)
                   ? float4(0.0f, 1.0f, 0.0f, 1.0f)
                   : float4(1.0f, 0.0f, 0.0f, 1.0f);

    // Draw 3×3 dot
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll] for (int dx = -1; dx <= 1; ++dx)
        {
            int2 px = center + int2(dx, dy);
            // Bounds check
            if (px.x < 0 || px.y < 0 ||
                px.x >= (int)g_OverlayCB.m_ViewportSize.x ||
                px.y >= (int)g_OverlayCB.m_ViewportSize.y)
                continue;
            g_Overlay[px] = color;
        }
    }
}
