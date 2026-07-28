// ShadowMask.hlsl — CSM shadow mask compute shader
// Supports PCF (mode 0) and EVSSM (mode 1) with optional screen-space contact shadows.
#include "Common.hlsli"
#include "CommonLighting.hlsli"
#include "srrhi/hlsl/Common.hlsli"
#include "srrhi/hlsl/ShadowMask.hlsli"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static const srrhi::ShadowMaskConstants g_CB             = srrhi::ShadowMaskInputs::GetCB();
static const Texture2D<float>           g_Depth          = srrhi::ShadowMaskInputs::GetDepth();
static const Texture2D<float2>          g_GBufferNormals = srrhi::ShadowMaskInputs::GetGBufferNormals();
static const Texture2DArray<float>      g_CSMShadowMap   = srrhi::ShadowMaskInputs::GetCSMShadowMap();
static const Texture2DArray<float4>     g_EVSMShadowMap  = srrhi::ShadowMaskInputs::GetEVSMShadowMap();
static const Texture2D<float2>          g_BlueNoiseTex   = srrhi::ShadowMaskInputs::GetBlueNoiseTexture();
static       RWTexture2D<float>         g_RWShadowMask   = srrhi::ShadowMaskInputs::GetRWShadowMask();
static const SamplerComparisonState     g_ShadowSampler  = srrhi::ShadowMaskInputs::GetShadowSampler();
static const SamplerState               g_LinearClamp    = srrhi::ShadowMaskInputs::GetLinearClamp();
static const SamplerState               g_PointClamp     = srrhi::ShadowMaskInputs::GetPointClamp();
static const SamplerState               g_PointWrap      = srrhi::ShadowMaskInputs::GetPointWrap();

// ---------------------------------------------------------------------------
// Cascade selection
// ---------------------------------------------------------------------------
uint SelectCascade(float viewDepth, float4 cascadeSplits)
{
    return dot(uint3(viewDepth >= cascadeSplits.xyz), 1);
}

// ---------------------------------------------------------------------------
// PCF — 4-tap bilinear Gaussian filter (Castaño, 2013 "Shadow Mapping Summary Part 1")
// Produces a smooth 3x3 Gaussian-weighted result with only 4 hardware PCF taps.
// ---------------------------------------------------------------------------
float ComputePCF(float3 shadowUV, float compareDepth, float texelSize)
{
    float size = (float)srrhi::CommonConsts::kShadowMapResolution;

    // Clamp to avoid overflows on some GPUs
    float2 pos = clamp(shadowUV.xy, -1.0f, 2.0f);

    float2 uv   = pos * size + 0.5f;
    float2 base = (floor(uv) - 0.5f) * texelSize;
    float2 st   = frac(uv);

    float2 uw = float2(3.0f - 2.0f * st.x, 1.0f + 2.0f * st.x);
    float2 vw = float2(3.0f - 2.0f * st.y, 1.0f + 2.0f * st.y);

    float2 u = float2((2.0f - st.x) / uw.x - 1.0f, st.x / uw.y + 1.0f) * texelSize;
    float2 v = float2((2.0f - st.y) / vw.x - 1.0f, st.y / vw.y + 1.0f) * texelSize;

    float slice = shadowUV.z;
    float sum = 0.0f;
    sum += uw.x * vw.x * (1.0f - g_CSMShadowMap.SampleCmpLevelZero(g_ShadowSampler, float3(base + float2(u.x, v.x), slice), compareDepth));
    sum += uw.y * vw.x * (1.0f - g_CSMShadowMap.SampleCmpLevelZero(g_ShadowSampler, float3(base + float2(u.y, v.x), slice), compareDepth));
    sum += uw.x * vw.y * (1.0f - g_CSMShadowMap.SampleCmpLevelZero(g_ShadowSampler, float3(base + float2(u.x, v.y), slice), compareDepth));
    sum += uw.y * vw.y * (1.0f - g_CSMShadowMap.SampleCmpLevelZero(g_ShadowSampler, float3(base + float2(u.y, v.y), slice), compareDepth));
    return sum * 0.0625f;  // 1/16 — normalizes the 4x4 bilinear weight sum
}

// ---------------------------------------------------------------------------
// Anisotropic normal bias
// ---------------------------------------------------------------------------
// b.xy = normalBias * texelSizeWorldSpace (pre-computed on CPU per cascade).
// Projects the world normal onto the shadow map's 2D grid (rows 0,1 of the
// row-major VP matrix) and computes the L1 norm of the anisotropic footprint.
// This inherently contains sin(theta) slope-scaling because the normal is unit-length.
float3 ApplyNormalBias(float3 worldPos, float3 worldNormal, float2 bias, float4x4 shadowVP)
{
    // Row 0 and Row 1 of the row-major VP matrix are the light X/Y basis in world space.
    float2 n_L = float2(
        dot(float3(shadowVP._11, shadowVP._12, shadowVP._13), worldNormal),
        dot(float3(shadowVP._21, shadowVP._22, shadowVP._23), worldNormal)
    );
    return worldPos + worldNormal * (abs(n_L.x * bias.x) + abs(n_L.y * bias.y));
}

// Unpack per-cascade float2 bias from the packed float4[2] array.
float2 GetCascadeNormalBias(uint cascadeIndex)
{
    // [0].xy = cascade 0, [0].zw = cascade 1, [1].xy = cascade 2, [1].zw = cascade 3
    float4 packed = g_CB.m_NormalBias[cascadeIndex >> 1];
    return (cascadeIndex & 1) ? packed.zw : packed.xy;
}

// ---------------------------------------------------------------------------
// Full CSM shadow evaluation (PCF path)
// ---------------------------------------------------------------------------
float ComputeCSMShadow(float3 worldPos, float3 worldNormal, float viewDepth)
{
    uint cascadeIndex = SelectCascade(viewDepth, g_CB.m_CascadeSplits);

    // Filament-style anisotropic normal bias (pre-computed on CPU)
    float2 bias           = GetCascadeNormalBias(cascadeIndex);
    float3 offsetWorldPos = ApplyNormalBias(worldPos, worldNormal, bias, g_CB.m_ShadowViewProj[cascadeIndex]);

    float4 lightSpacePos = mul(float4(offsetWorldPos, 1.0f), g_CB.m_ShadowViewProj[cascadeIndex]);
    float3 shadowUV      = lightSpacePos.xyz / lightSpacePos.w;
    shadowUV.xy          = shadowUV.xy * float2(0.5f, -0.5f) + 0.5f;
    shadowUV.z          += g_CB.m_ConstantDepthBias; // Reversed-Z + GREATER comparison: push receiver toward near (higher Z) to reduce acne

    if (any(shadowUV.xy < 0.0f) || any(shadowUV.xy > 1.0f))
        return 1.0f;

    float texelSize = 1.0f / (float)srrhi::CommonConsts::kShadowMapResolution;
    return ComputePCF(float3(shadowUV.xy, (float)cascadeIndex), shadowUV.z, texelSize);
}

// ---------------------------------------------------------------------------
// Interleaved Gradient Noise
// ---------------------------------------------------------------------------
float InterleavedGradientNoise(float2 fragCoord)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(fragCoord, magic.xy)));
}

// ---------------------------------------------------------------------------
// Chebyshev upper bound
// ---------------------------------------------------------------------------
float ChebyshevUpperBound(float2 moments, float depth, float minVariance, float lbrAmount)
{
    if (depth <= moments.x)
        return 1.0f;
    float variance = max(moments.y - moments.x * moments.x, minVariance);
    float d        = depth - moments.x;
    float p_max    = variance / (variance + d * d);
    return saturate((p_max - lbrAmount) / max(1.0f - lbrAmount, 1e-6f));
}

// ---------------------------------------------------------------------------
// ELVSM evaluation
// ---------------------------------------------------------------------------
float EvaluateEVSMFull(float c, float4 moments, float zReceiver, float lbrAmount)
{
    const float EPSILON_MULTIPLIER = 0.002f;
    float depth = zReceiver * 2.0f - 1.0f;

    float pw           = exp( c * depth);
    float pMinVariance = EPSILON_MULTIPLIER * (pw * pw);
    float p            = ChebyshevUpperBound(moments.xy, pw, pMinVariance, lbrAmount);

    float nw           = exp(-c * depth);
    float nMinVariance = EPSILON_MULTIPLIER * (nw * nw);
    float n            = ChebyshevUpperBound(moments.zw, nw, nMinVariance, lbrAmount);

    return min(p, n);
}

// ---------------------------------------------------------------------------
// EVSSM — full soft shadow (directional CSM, 5-tap Quincunx)
// ---------------------------------------------------------------------------
float ComputeEVSSMShadow(float3 worldPos, float3 worldNormal, float viewDepth)
{
    uint cascadeIndex = SelectCascade(viewDepth, g_CB.m_CascadeSplits);

    // Filament-style anisotropic normal bias (pre-computed on CPU)
    float2 bias      = GetCascadeNormalBias(cascadeIndex);
    float3 offsetPos = ApplyNormalBias(worldPos, worldNormal, bias, g_CB.m_ShadowViewProj[cascadeIndex]);

    float4 lsPos = mul(float4(offsetPos, 1.0f), g_CB.m_ShadowViewProj[cascadeIndex]);
    float3 uvz   = lsPos.xyz / lsPos.w;
    uvz.xy       = uvz.xy * float2(0.5f, -0.5f) + 0.5f;
    uvz.z       += g_CB.m_ConstantDepthBias; // Reversed-Z + GREATER comparison: push receiver toward near (higher Z) to reduce acne

    if (any(uvz.xy < 0.0f) || any(uvz.xy > 1.0f))
        return 1.0f;

    float  shadowDepth        = uvz.z;
    float2 uv                 = uvz.xy;
    float  slice              = (float)cascadeIndex;
    float  texelSize          = 1.0f / (float)srrhi::CommonConsts::kShadowMapResolution;

    // Compute world-space-to-texel conversion for penumbra estimation (not bias-related)
    float3 lightX             = float3(g_CB.m_ShadowViewProj[cascadeIndex]._11, g_CB.m_ShadowViewProj[cascadeIndex]._12, g_CB.m_ShadowViewProj[cascadeIndex]._13);
    float  wsOneOverTexelSize = srrhi::CommonConsts::kShadowMapResolution * max(length(lightX), 1e-10f) * 0.5f;

    float proj             = g_CB.m_ProjectionParam[cascadeIndex];
    float distToNear       = shadowDepth * proj;
    float physSearchRadius = min(g_CB.m_BulbRadius * distToNear, g_CB.m_MaxSearchRadius);
    float searchRadiusTex  = physSearchRadius * wsOneOverTexelSize;
    float searchLod        = clamp(log2(max(searchRadiusTex, 1.0f)), 0.0f, g_CB.m_MaxMipLevel);

    float4 searchMoments = g_EVSMShadowMap.SampleLevel(g_LinearClamp, float3(uv, slice), searchLod);
    float  negMoment     = max(-searchMoments.z, 1e-8f);
    float  zBlocker      = clamp((log(negMoment) / -g_CB.m_VsmExponent + 1.0f) * 0.5f, 0.0f, shadowDepth);

    float geoRatio = (shadowDepth - zBlocker) * proj * g_CB.m_PenumbraRatioScale;
    if (geoRatio > g_CB.m_MaxPenumbraRatio)
        geoRatio = g_CB.m_MaxPenumbraRatio + (1.0f - exp(-(geoRatio - g_CB.m_MaxPenumbraRatio)));

    float penumbraWidthTex = geoRatio * g_CB.m_BulbRadius * wsOneOverTexelSize;

    float idealLod   = log2(max(penumbraWidthTex, 1.0f)) - 1.0f;
    float targetLod  = clamp(idealLod, 0.0f, g_CB.m_MaxMipLevel);
    float lodDeficit = exp2(max(idealLod - targetLod, 0.0f));

    float2 r    = (penumbraWidthTex * texelSize) * 0.5f * lodDeficit;
    float2 rotX = float2(r.x, 0.0f);
    float2 rotY = float2(0.0f, r.y);

    #define EVSSM_FETCH(off) g_EVSMShadowMap.SampleLevel(g_LinearClamp, float3(clamp(uv + (off), 0.0f, 1.0f), slice), targetLod)
    float4 finalMoments = EVSSM_FETCH(float2(0.0f, 0.0f)) * 0.5f
                        + (EVSSM_FETCH( rotX + rotY) + EVSSM_FETCH( rotX - rotY)
                        +  EVSSM_FETCH(-rotX + rotY) + EVSSM_FETCH(-rotX - rotY)) * 0.125f;
    #undef EVSSM_FETCH

    return EvaluateEVSMFull(g_CB.m_VsmExponent, finalMoments, shadowDepth, g_CB.m_LightBleedReduction);
}

// ---------------------------------------------------------------------------
// Screen-Space Contact Shadows
// ---------------------------------------------------------------------------
float ComputeContactShadow(float3 worldPos, float viewDepth, float noiseSample)
{
    // Ray end in world space — march toward the light
    float3 wsEnd = worldPos + g_CB.m_LightDirectionWS * g_CB.m_ContactShadowDistance;

    // Project start/end to clip space
    float4 csStart = mul(float4(worldPos, 1.0f), g_CB.m_WorldToClip);
    float4 csEnd   = mul(float4(wsEnd,    1.0f), g_CB.m_WorldToClip);

    // Perspective divide → NDC
    float3 ssStart = csStart.xyz / csStart.w;
    float3 ssEnd   = csEnd.xyz   / csEnd.w;

    // UV space: D3D NDC xy [-1,1] -> [0,1] with Y flip; z stays as NDC depth (reversed-Z)
    float3 uvStart = float3(ClipXYToUV(ssStart.xy), ssStart.z);
    float3 uvRay   = float3(ClipXYToUV(ssEnd.xy), ssEnd.z) - uvStart;

    float dt = 1.0f / (float)g_CB.m_ContactShadowSteps;

    // Tolerance: compute how much NDC Z changes for one step
    // along the view-Z axis. For infinite reversed-Z: NDC_Z = near / viewZ.
    // A point at (viewZ + stepDist) has NDC_Z = near / (viewZ + stepDist).
    // Full ray delta = near/viewZ - near/(viewZ + dist) = near*dist / (viewZ*(viewZ+dist))
    // Per-step tolerance = fullDelta * dt
    // We use viewDepth (positive, LH) directly.
    float dist = g_CB.m_ContactShadowDistance;
    float tolerance = abs(ssStart.z - ssStart.z * viewDepth / (viewDepth + dist)) * dt;

    // Clamp minimum tolerance to prevent false hits from float32 precision noise
    tolerance = max(tolerance, ssStart.z * 5e-5f);

    // Dithered start position to break banding
    float t = dt * noiseSample + dt;

    float  occlusion = 0.0f;
    float3 ray       = uvStart;
    for (uint i = 0; i < g_CB.m_ContactShadowSteps; i++, t += dt)
    {
        ray      = uvStart + uvRay * t;
        float z  = g_Depth.SampleLevel(g_PointClamp, ray.xy, 0).r;
        float dz = z - ray.z;
        if (abs(tolerance - dz) < tolerance)
        {
            occlusion = 1.0f;
            break;
        }
    }

    // Fade out near screen edges where depth data is unavailable
    float2 fade = max(12.0f * abs(ray.xy - 0.5f) - 5.0f, 0.0f);
    return occlusion * saturate(1.0f - dot(fade, fade));
}

[numthreads(8, 8, 1)]
void ShadowMask_CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 uvInt = dispatchID.xy;
    if (any(uvInt >= uint2(g_CB.m_OutputSize))) return;

    float depth = g_Depth.Load(uint3(uvInt, 0));
    if (depth == srrhi::CommonConsts::DEPTH_FAR) { g_RWShadowMask[uvInt] = 1.0f; return; }

    float2 uv       = (float2(uvInt) + 0.5f) / g_CB.m_OutputSize;
    float3 worldPos = ReconstructWorldPos(uv, depth, g_CB.m_ClipToWorld);
    float3 worldNorm = DecodeOct(g_GBufferNormals.Load(uint3(uvInt, 0)).rg);
    float  viewDepth = mul(float4(worldPos, 1.0f), g_CB.m_WorldToView).z;

    float shadow;
#if EVSSM
    shadow = ComputeEVSSMShadow(worldPos, worldNorm, viewDepth);
#else
    shadow = ComputeCSMShadow(worldPos, worldNorm, viewDepth);
#endif

    if (g_CB.m_ContactShadowSteps > 0u)
    {
        float noise;
        if (g_CB.m_ContactShadowsUseBlueNoise)
        {
            uint2 bnCoord = (uvInt + uint2(g_CB.m_FrameIndex * 17u, g_CB.m_FrameIndex * 31u)) & 0x3F;
            noise = g_BlueNoiseTex.SampleLevel(g_PointWrap, float2(bnCoord) / 64.0f, 0).r - 0.5f;
        }
        else
        {
            noise = InterleavedGradientNoise(float2(uvInt) + 5.588238f * float(g_CB.m_FrameIndex)) - 0.5f;
        }
        shadow *= (1.0f - ComputeContactShadow(worldPos, viewDepth, noise));
    }

    g_RWShadowMask[uvInt] = shadow;
}
