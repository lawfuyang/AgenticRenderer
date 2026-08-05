// DDGI Probe Vis — TLAS Instance Transform Update
// One thread per probe.  Writes sphere instance transforms (position + radius scale)
// into a RWByteAddressBuffer backing the TLAS instance-desc buffer.
// Based on RTXGI-DDGI test-harness ProbesUpdateCS.hlsl.

#include "DDGIShaderConfig.h"
#include "ProbeCommon.hlsl"

#include "../srrhi/hlsl/ProbeVisUpdate.hlsli"

static const srrhi::ProbeVisUpdateConstants                     g_CB        = srrhi::ProbeVisUpdateInputs::GetProbeVisUpdateCB();
static const StructuredBuffer<DDGIVolumeDescGPUPacked>          g_Volumes   = srrhi::ProbeVisUpdateInputs::GetDDGIVolumes();
static       RWByteAddressBuffer                                g_Instances = srrhi::ProbeVisUpdateInputs::GetInstanceBuffer();

[numthreads(32, 1, 1)]
void ProbeVisUpdateCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_Volumes[g_CB.m_VolumeIndex]);

    uint numProbes = volume.probeCounts.x * volume.probeCounts.y * volume.probeCounts.z;
    if (dispatchThreadID.x >= numProbes)
        return;

    int3 probeCoords = DDGIGetProbeCoords((int)dispatchThreadID.x, volume);

    // Compute world position from grid coords (without relocation offset for first pass)
    float3 worldPos = volume.origin
        + float3(probeCoords) * volume.probeSpacing
        + volume.probeScrollOffsets * volume.probeSpacing;

    float  r         = g_CB.m_ProbeRadius;
    uint   baseAddr  = (g_CB.m_InstanceOffset + dispatchThreadID.x) * 48u;

    // Row 0: { scaleX, 0, 0, translateX }
    g_Instances.Store<float4>(baseAddr,      float4(r,    0.f, 0.f, worldPos.x));
    // Row 1: { 0, scaleY, 0, translateY }
    g_Instances.Store<float4>(baseAddr + 16u, float4(0.f,  r,   0.f, worldPos.y));
    // Row 2: { 0, 0, scaleZ, translateZ }
    g_Instances.Store<float4>(baseAddr + 32u, float4(0.f,  0.f, r,   worldPos.z));
}
