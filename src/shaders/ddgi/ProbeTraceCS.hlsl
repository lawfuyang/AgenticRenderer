// DDGI Probe Trace Compute Shader
// One thread per probe ray. Dispatched as (totalProbes * probeNumRays / 64, 1, 1),
// i.e. ceil(totalProbes * probeNumRays / 64) groups of 64 threads each.
// Traces a single probe ray via inline RT, evaluates direct sun lighting at the
// hit point and writes radiance+hitT into the RayData texture array (F32x2 format).

#include "DDGIShaderConfig.h"

// SDK helpers: ProbeCommon includes ProbeRayCommon, ProbeIndexing, ProbeOctahedral
#include "ProbeCommon.hlsl"
#include "ddgi/Irradiance.hlsl"

#include "../RaytracingCommon.hlsli"
#include "../CommonLighting.hlsli"
#include "../Atmosphere.hlsli"
#include "../Bindless.hlsli"

#include "../srrhi/hlsl/ProbeTrace.hlsli"

// ─── Resource accessors ──────────────────────────────────────────────────────
static const srrhi::ProbeTraceConstants                     g_ProbeCB    = srrhi::ProbeTraceInputs::GetProbeTraceCB();
static const StructuredBuffer<DDGIVolumeDescGPUPacked>      g_Volumes    = srrhi::ProbeTraceInputs::GetDDGIVolumes();
static const RaytracingAccelerationStructure                g_SceneAS    = srrhi::ProbeTraceInputs::GetSceneAS();
static const StructuredBuffer<srrhi::GPULight>              g_Lights     = srrhi::ProbeTraceInputs::GetLights();
static const StructuredBuffer<srrhi::PerInstanceData>       g_Instances  = srrhi::ProbeTraceInputs::GetInstances();
static const StructuredBuffer<srrhi::MeshData>              g_MeshData   = srrhi::ProbeTraceInputs::GetMeshData();
static const StructuredBuffer<srrhi::MaterialConstants>     g_Materials  = srrhi::ProbeTraceInputs::GetMaterials();
static const StructuredBuffer<uint>                         g_Indices    = srrhi::ProbeTraceInputs::GetIndices();
static const StructuredBuffer<srrhi::VertexQuantized>       g_Vertices   = srrhi::ProbeTraceInputs::GetVertices();
static const Texture2DArray<float4>                         g_ProbeData  = srrhi::ProbeTraceInputs::GetProbeData();
static       RWTexture2DArray<float4>                       g_RayData    = srrhi::ProbeTraceInputs::GetRayData();

// ── Back-propagation helper ──────────────────────────────────────────────
// File-scope wrapper that builds DDGIVolumeResources from bindless heap indices,
// then calls the SDK's DDGIGetVolumeIrradiance().  Uses the same pattern as
// SampleDDGIAtlas() — SamplerState capture works at file scope.
float3 SampleVolumeIrradiance(float3 worldPos, float3 normal, float3 cameraDir, DDGIVolumeDescGPU volume)
{
    DDGIVolumeResources res;
    res.probeIrradiance  = ResourceDescriptorHeap[NonUniformResourceIndex(g_ProbeCB.m_IrradianceTexIndex)];
    res.probeDistance    = ResourceDescriptorHeap[NonUniformResourceIndex(g_ProbeCB.m_DistanceTexIndex)];
    res.probeData        = ResourceDescriptorHeap[NonUniformResourceIndex(g_ProbeCB.m_ProbeDataTexIndex)];
    res.bilinearSampler  = SamplerDescriptorHeap[NonUniformResourceIndex(srrhi::CommonConsts::SAMPLER_LINEAR_CLAMP_INDEX)];

    float3 bias = DDGIGetSurfaceBias(normal, cameraDir, volume);
    return DDGIGetVolumeIrradiance(worldPos, bias, normal, volume, res);
}

// ─── Sun shadow helper ───────────────────────────────────────────────────────
// Returns 1.0 if lit, 0.0 if occluded.
float TraceSunShadow(float3 worldPos, float3 surfaceNormal, float3 sunDir)
{
    static const float kBias = 0.01f;
    RayDesc ray;
    ray.Origin    = worldPos + surfaceNormal * kBias;
    ray.Direction = sunDir;
    ray.TMin      = kBias;
    ray.TMax      = 1e10f;

    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(g_SceneAS, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();
    return (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0f : 1.0f;
}

// ─── Entry point ─────────────────────────────────────────────────────────────
[numthreads(64, 1, 1)]
void ProbeTraceCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(g_Volumes[g_ProbeCB.m_VolumeIndex]);

    int totalProbes = volume.probeCounts.x * volume.probeCounts.y * volume.probeCounts.z;
    int numRays     = volume.probeNumRays;

    // Thread→(probe, ray) decomposition
    int globalIdx  = (int)dispatchThreadID.x;
    int probeIndex = globalIdx / numRays;
    int rayIndex   = globalIdx % numRays;

    if (probeIndex >= totalProbes)
        return;

    // Probe grid coordinates and scroll-adjusted index
    int3 probeCoords         = DDGIGetProbeCoords(probeIndex, volume);
    int  scrolledProbeIndex  = DDGIGetScrollingProbeIndex(probeCoords, volume);

    // Skip inactive probes (fixed rays must still run for relocation/classification)
    float probeState = DDGILoadProbeState(scrolledProbeIndex, g_ProbeData, volume);
    if (probeState == RTXGI_DDGI_PROBE_STATE_INACTIVE && rayIndex >= RTXGI_DDGI_NUM_FIXED_RAYS)
        return;

    // World position with relocation offset
    float3 probeWorldPos = DDGIGetProbeWorldPosition(probeCoords, volume, g_ProbeData);

    // Ray direction: randomly rotated Fibonacci sphere sample
    float3 rayDir = DDGIGetProbeRayDirection(rayIndex, volume);

    // Output texel in RayData
    uint3 outputCoords = DDGIGetRayDataTexelCoords(rayIndex, scrolledProbeIndex, volume);

    // ── Inline ray trace ──────────────────────────────────────────────────
    RayDesc ray;
    ray.Origin    = probeWorldPos;
    ray.Direction = rayDir;
    ray.TMin      = 0.001f;
    ray.TMax      = volume.probeMaxRayDistance;

    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(g_SceneAS, RAY_FLAG_NONE, 0xFF, ray);

    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            uint instanceIndex  = q.CandidateInstanceIndex();
            uint primitiveIndex = q.CandidatePrimitiveIndex();
            float2 bary         = q.CandidateTriangleBarycentrics();

            srrhi::PerInstanceData inst = g_Instances[instanceIndex];
            srrhi::MeshData mesh        = g_MeshData[inst.m_MeshDataIndex];
            srrhi::MaterialConstants mat= g_Materials[inst.m_MaterialIndex];

            if (mat.m_AlphaMode == srrhi::CommonConsts::ALPHA_MODE_MASK)
            {
                float2 uv = GetInterpolatedUV(primitiveIndex, 0, bary, mesh, g_Indices, g_Vertices);
                if (AlphaTest(uv, mat))
                    q.CommitNonOpaqueTriangleHit();
            }
        }
    }

    // ── Miss: store sky radiance ──────────────────────────────────────────
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        // Probe is at an arbitrary world position; use origin as camera stand-in.
        // Shadow-length is 0 (no cloud shadows on miss).
        float3 skyRadiance = GetAtmosphereSkyRadiance(
            probeWorldPos, rayDir,
            g_ProbeCB.m_SunDirection, g_ProbeCB.m_SunIntensity,
            /*bAddSunDisk=*/false);
        DDGIStoreProbeRayMiss(g_RayData, outputCoords, volume, skyRadiance);
        return;
    }

    float hitT = q.CommittedRayT();

    // ── Back-face hit: store negative hitT ───────────────────────────────
    if (!q.CommittedTriangleFrontFace())
    {
        DDGIStoreProbeRayBackfaceHit(g_RayData, outputCoords, volume, hitT);
        return;
    }

    // ── Fixed-ray front-face hit: hitT only (no lighting) ────────────────
    if ((volume.probeRelocationEnabled || volume.probeClassificationEnabled) && rayIndex < RTXGI_DDGI_NUM_FIXED_RAYS)
    {
        DDGIStoreProbeRayFrontfaceHit(g_RayData, outputCoords, volume, hitT);
        return;
    }

    // ── Front-face hit: evaluate direct lighting ──────────────────────────
    uint instanceIndex  = q.CommittedInstanceIndex();
    uint primitiveIndex = q.CommittedPrimitiveIndex();
    float2 bary         = q.CommittedTriangleBarycentrics();

    srrhi::PerInstanceData inst = g_Instances[instanceIndex];
    srrhi::MeshData mesh        = g_MeshData[inst.m_MeshDataIndex];
    srrhi::MaterialConstants mat= g_Materials[inst.m_MaterialIndex];

    RayHitInfo hitInfo;
    hitInfo.m_InstanceIndex  = instanceIndex;
    hitInfo.m_PrimitiveIndex = primitiveIndex;
    hitInfo.m_Barycentrics   = bary;
    hitInfo.m_RayT           = hitT;

    FullHitAttributes attr = GetFullHitAttributes(hitInfo, ray, inst, mesh, g_Indices, g_Vertices);
    PBRAttributes     pbr  = GetPBRAttributes(attr, mat);

    // Sun direct lighting
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    float3 sunDir   = g_ProbeCB.m_SunDirection;
    float  NdotL    = saturate(dot(pbr.normal, sunDir));

    if (NdotL > 0.0f && g_ProbeCB.m_LightCount > 0)
    {
        float  shadow     = TraceSunShadow(attr.m_WorldPos, pbr.normal, sunDir);
        float3 sunRadiance = GetAtmosphereSunRadiance(GetAtmospherePos(attr.m_WorldPos), sunDir, g_ProbeCB.m_SunIntensity);

        float3 diffuseAlbedo, specularF0;
        GetReflectivityFromMetallic(pbr.metallic, pbr.baseColor, diffuseAlbedo, specularF0);

        // Diffuse BRDF * NdotL (probes capture diffuse irradiance only)
        float diffuseTerm = max(NdotL, 0.0f) / srrhi::CommonConsts::M_PI;
        radiance = diffuseAlbedo * diffuseTerm * sunRadiance * shadow;
    }

    // Emissive surfaces emit their own light
    radiance += pbr.emissive;

    // Prevent energy amplification
    static const float kMaxAlbedo = 0.9f;
    radiance = min(radiance, float3(kMaxAlbedo, kMaxAlbedo, kMaxAlbedo));

    // ── Back-propagation: sample the volume's irradiance at the hit point ─
    // This is how DDGI achieves recursive multi-bounce indirect lighting.
    // The irradiance atlas contains last frame's accumulated result; blending
    // propagates it forward each frame.
    float3 cameraDir = normalize(probeWorldPos - attr.m_WorldPos);
    float3 indirect  = SampleVolumeIrradiance(attr.m_WorldPos, pbr.normal, cameraDir, volume);
    radiance += indirect;

    DDGIStoreProbeRayFrontfaceHit(g_RayData, outputCoords, volume, radiance, hitT);
}
