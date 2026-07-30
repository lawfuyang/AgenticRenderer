// ShadowMask.hlsl — CSM shadow mask compute shader
// Supports PCF (mode 0) and EVSSM (mode 1).
// Screen-space shadows are handled separately by ScreenSpaceShadows.hlsl.
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
static       RWTexture2D<float>         g_RWShadowMask   = srrhi::ShadowMaskInputs::GetRWShadowMask();
static const SamplerComparisonState     g_ShadowSampler  = srrhi::ShadowMaskInputs::GetShadowSampler();
static const SamplerState               g_LinearClamp    = srrhi::ShadowMaskInputs::GetLinearClamp();

// ---------------------------------------------------------------------------
// Cascade selection
// ---------------------------------------------------------------------------
uint SelectCascade(float viewDepth)
{
    return dot(uint3(viewDepth >= g_CB.m_CascadeSplits.xyz), 1);
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
float3 ApplyNormalBias(float3 worldPos, float3 worldNormal, float2 bias, uint cascadeIndex)
{
    float4x4 shadowVP = g_CB.m_ShadowViewProj[cascadeIndex];
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
    uint cascadeIndex = SelectCascade(viewDepth);

    // Anisotropic normal bias (pre-computed on CPU)
    float2 bias           = GetCascadeNormalBias(cascadeIndex);
    float3 offsetWorldPos = ApplyNormalBias(worldPos, worldNormal, bias, cascadeIndex);

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
// Chebyshev upper bound
// ---------------------------------------------------------------------------
float ChebyshevUpperBound(float2 moments, float depth, float minVariance, float lbrAmount)
{
    if (depth <= moments.x)
        return 1.0f;
    float variance = max(moments.y - moments.x * moments.x, minVariance);
    float d        = depth - moments.x;
    float p_max    = variance / (variance + d * d);
    return saturate((p_max - lbrAmount) / (1.0f - lbrAmount));
}

// ---------------------------------------------------------------------------
// EVSM evaluation
// zReceiver is in [0,1] linear light-space (0=near, 1=far)
// ---------------------------------------------------------------------------
float EvaluateEVSM(float4 moments, float zReceiver)
{
    const float EPSILON_MULTIPLIER = 0.00001f; // fp32 moments: tighter variance floor than fp16's 0.002 -> less light bleeding
    // Remap to [-1, 1]: near(0)->-1, far(1)->+1
    float depth = zReceiver * 2.0f - 1.0f;

    // Positive warp
    float pw           = exp(g_CB.m_VsmExponent * depth);
    float pMinVariance = EPSILON_MULTIPLIER * (pw * pw);
    float p            = ChebyshevUpperBound(moments.xy, pw, pMinVariance, g_CB.m_LightBleedReduction);

    // Negative warp: nw = -1/pw (stored negative in texture)
    float nw           = -1.0f / pw;
    float nMinVariance = EPSILON_MULTIPLIER * (nw * nw);
    float n            = ChebyshevUpperBound(moments.zw, nw, nMinVariance, g_CB.m_LightBleedReduction);

    return min(p, n);
}

// ---------------------------------------------------------------------------
// EVSSM — full soft shadow (directional CSM, 5-tap Quincunx)
// ---------------------------------------------------------------------------
float ComputeEVSSMShadow(float3 worldPos, float3 worldNormal, float viewDepth)
{
    uint cascadeIndex = SelectCascade(viewDepth);

    // Anisotropic normal bias (pre-computed on CPU)
    float2 bias      = GetCascadeNormalBias(cascadeIndex);
    float3 offsetPos = ApplyNormalBias(worldPos, worldNormal, bias, cascadeIndex);

    float4 lsPos = mul(float4(offsetPos, 1.0f), g_CB.m_ShadowViewProj[cascadeIndex]);
    float3 uvz   = lsPos.xyz / lsPos.w;
    uvz.xy       = uvz.xy * float2(0.5f, -0.5f) + 0.5f;

    if (any(uvz.xy < 0.0f) || any(uvz.xy > 1.0f))
        return 1.0f;

    // Convert reversed-Z NDC depth [1=near, 0=far] to linear [0=near, 1=far]
    float  shadowDepth        = 1.0f - uvz.z;
    float2 uv                 = uvz.xy;
    float  slice              = (float)cascadeIndex;
    float  texelSize          = 1.0f / (float)srrhi::CommonConsts::kShadowMapResolution;

    // Compute world-space-to-texel conversion for penumbra estimation
    float3 lightX             = float3(g_CB.m_ShadowViewProj[cascadeIndex]._11, g_CB.m_ShadowViewProj[cascadeIndex]._12, g_CB.m_ShadowViewProj[cascadeIndex]._13);
    float  wsOneOverTexelSize = srrhi::CommonConsts::kShadowMapResolution * max(length(lightX), 1e-10f) * 0.5f;

    // projectionParam = (far - near) for directional lights
    // shadowDepth * projectionParam = distance from near plane in world units
    float proj             = g_CB.m_ProjectionParam[cascadeIndex];
    float distToNear       = shadowDepth * proj;
    float physSearchRadius = min(g_CB.m_BulbRadius * distToNear, g_CB.m_MaxSearchRadius);
    float searchRadiusTex  = physSearchRadius * wsOneOverTexelSize;
    float searchLod        = clamp(log2(max(searchRadiusTex, 1.0f)), 0.0f, g_CB.m_MaxMipLevel);

    float4 searchMoments = g_EVSMShadowMap.SampleLevel(g_LinearClamp, float3(uv, slice), searchLod);
    // Negative warp is stored as negative value; negate to get positive for log
    float  negMoment     = max(-searchMoments.z, 1e-8f);
    // Recover blocker depth: negMoment = exp(-c * (zBlocker*2-1)) => solve for zBlocker
    float  zBlocker      = clamp((log(negMoment) / -g_CB.m_VsmExponent + 1.0f) * 0.5f, 0.0f, shadowDepth);

    // Penumbra estimation
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

    // Debug visualizations — write a normalized [0,1] quantity into the mask so the
    // CSMDebug pass can heat-map it. These override the shadow factor only in debug modes.
    if (g_CB.m_CSMDebugMode == srrhi::CSMDebugMode::CSM_DEBUG_EVSSM_PENUMBRA)
        return saturate(penumbraWidthTex / 64.0f);           // penumbra width in texels, normalized to 64
    if (g_CB.m_CSMDebugMode == srrhi::CSMDebugMode::CSM_DEBUG_EVSSM_BLOCKER)
        return saturate((shadowDepth - zBlocker) * proj / 10.0f); // receiver->blocker world distance, normalized to 10m
    if (g_CB.m_CSMDebugMode == srrhi::CSMDebugMode::CSM_DEBUG_EVSSM_TARGET_LOD)
        return saturate(targetLod / max(g_CB.m_MaxMipLevel, 1.0f));

    return EvaluateEVSM(finalMoments, shadowDepth);
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

    g_RWShadowMask[uvInt] = shadow;
}
