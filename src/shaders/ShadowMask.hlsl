// ShadowMask.hlsl — CSM shadow mask compute shader
// Supports PCF soft shadows.
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
static       RWTexture2D<float>         g_RWShadowMask   = srrhi::ShadowMaskInputs::GetRWShadowMask();
static const SamplerComparisonState     g_ShadowSampler  = srrhi::ShadowMaskInputs::GetShadowSampler();

// ---------------------------------------------------------------------------
// Cascade selection
// ---------------------------------------------------------------------------
uint SelectCascade(float viewDepth)
{
    return dot(uint3(viewDepth >= g_CB.m_CascadeSplits.xyz), 1);
}

// ---------------------------------------------------------------------------
// PCF filter selection
//   1 = Rotated Poisson disk, 16 taps, per-pixel random rotation
//   0 = 9-tap bilinear Gaussian (Castaño / The Witness)
// ---------------------------------------------------------------------------
#define PCF_ROTATED_POISSON 1

float SamplePCFTap(float2 base, float2 offset, float texelSize, float slice, float compareDepth)
{
    float3 uv = float3(base + offset * texelSize, slice);
    return 1.0f - g_CSMShadowMap.SampleCmpLevelZero(g_ShadowSampler, uv, compareDepth);
}

#if PCF_ROTATED_POISSON

// ---------------------------------------------------------------------------
// PCF — 16-tap rotated Poisson disk
// Kernel radius in texels. Larger = softer penumbra but more visible noise.
static const float kPoissonRadiusTexels = 4.0f;

// Poisson disk offsets on the unit disk, stratified from MJP's 64-tap progressive
// table (every 4th entry) so all quadrants are covered. Taking the raw leading 16
// entries of that table would bias the kernel toward -X and skew the penumbra.
static const float2 kPoissonDisk16[16] =
{
    float2(-0.5119625f,  -0.4827938f),
    float2(-0.5938849f,  -0.6895654f),
    float2(-0.8409063f,  -0.03465778f),
    float2(-0.1041822f,  -0.02521214f),
    float2(-0.9879128f,   0.1113683f),
    float2(-0.1925334f,   0.1787288f),
    float2( 0.1975043f,   0.2221317f),
    float2( 0.1558783f,  -0.08460935f),
    float2( 0.3956799f,  -0.1469177f),
    float2( 0.3928624f,  -0.4417621f),
    float2( 0.0005130528f, -0.8058334f),
    float2( 0.4060591f,  -0.7100726f),
    float2( 0.1268544f,  -0.9874692f),
    float2( 0.5663417f,   0.7708698f),
    float2( 0.4216993f,   0.9002838f),
    float2(-0.09640376f,  0.9843736f),
};

// Per-pixel rotation angle. Hashing the pixel coordinate turns the fixed 16-tap
// pattern into an effectively unique kernel per pixel, trading banding for
// high-frequency noise that reads as film grain (and is TAA-resolvable).
// Note: m_FrameIndex is deliberately NOT hashed in — a per-frame-varying kernel
// makes static shadows crawl when there is no temporal filter on this mask.
float2 GetPoissonRotation(uint2 pixelCoord)
{
    uint  h     = PCGHash(pixelCoord.x + PCGHash(pixelCoord.y));
    h = h ^ PCGHash(g_CB.m_FrameIndex);
    float theta = (float)h * (1.0f / 4294967296.0f) * 6.28318530718f;
    return float2(cos(theta), sin(theta));
}

float ComputePCF(float3 shadowUV, float compareDepth, float texelSize, uint2 pixelCoord)
{
    float2 cs    = GetPoissonRotation(pixelCoord);
    float  slice = shadowUV.z;
    float  sum   = 0.0f;

    [unroll]
    for (uint i = 0; i < 16; ++i)
    {
        // Rotate the disk offset, then scale into texel space.
        float2 o = kPoissonDisk16[i];
        float2 rotated = float2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);
        sum += SamplePCFTap(shadowUV.xy, rotated * kPoissonRadiusTexels, texelSize, slice, compareDepth);
    }

    return sum * (1.0f / 16.0f);
}

#else // PCF_ROTATED_POISSON

// ---------------------------------------------------------------------------
// PCF — 9-tap bilinear Gaussian filter (Castaño, 2013 "Shadow Mapping Summary Part 1")
// Produces a smooth 5x5 Gaussian-weighted result with only 9 hardware PCF taps.
// This is the filter shipped in The Witness and used by Unity since 5.0.
// ---------------------------------------------------------------------------
float ComputePCF(float3 shadowUV, float compareDepth, float texelSize, uint2 pixelCoord)
{
    float size = (float)srrhi::CommonConsts::kShadowMapResolution;

    // Clamp to avoid overflows on some GPUs
    float2 pos = clamp(shadowUV.xy, -1.0f, 2.0f);

    float2 uv   = pos * size + 0.5f;
    float2 base = (floor(uv) - 0.5f) * texelSize;
    float2 st   = frac(uv);

    // Separable 5x5 kernel with taps (a, b, c) = (1, 3, 4) — a crude Gaussian.
    // Each PCF tap covers a non-overlapping 2x2 footprint; uw/vw are the tap weights and
    // u/v the sub-texel offsets that reproduce the exact Gaussian-weighted 5x5 sum.
    float3 uw = float3(4.0f - 3.0f * st.x, 7.0f, 1.0f + 3.0f * st.x);
    float3 vw = float3(4.0f - 3.0f * st.y, 7.0f, 1.0f + 3.0f * st.y);

    float3 u = float3((3.0f - 2.0f * st.x) / uw.x - 2.0f, (3.0f + st.x) / uw.y, st.x / uw.z + 2.0f);
    float3 v = float3((3.0f - 2.0f * st.y) / vw.x - 2.0f, (3.0f + st.y) / vw.y, st.y / vw.z + 2.0f);

    float slice = shadowUV.z;
    float sum = 0.0f;
    sum += uw.x * vw.x * SamplePCFTap(base, float2(u.x, v.x), texelSize, slice, compareDepth);
    sum += uw.y * vw.x * SamplePCFTap(base, float2(u.y, v.x), texelSize, slice, compareDepth);
    sum += uw.z * vw.x * SamplePCFTap(base, float2(u.z, v.x), texelSize, slice, compareDepth);

    sum += uw.x * vw.y * SamplePCFTap(base, float2(u.x, v.y), texelSize, slice, compareDepth);
    sum += uw.y * vw.y * SamplePCFTap(base, float2(u.y, v.y), texelSize, slice, compareDepth);
    sum += uw.z * vw.y * SamplePCFTap(base, float2(u.z, v.y), texelSize, slice, compareDepth);

    sum += uw.x * vw.z * SamplePCFTap(base, float2(u.x, v.z), texelSize, slice, compareDepth);
    sum += uw.y * vw.z * SamplePCFTap(base, float2(u.y, v.z), texelSize, slice, compareDepth);
    sum += uw.z * vw.z * SamplePCFTap(base, float2(u.z, v.z), texelSize, slice, compareDepth);

    return sum * (1.0f / 144.0f);  // 12x12 — total bilinear weight is constant in st
}

#endif // #else PCF_ROTATED_POISSON

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
float ComputeCSMShadow(float3 worldPos, float3 worldNormal, float viewDepth, uint2 pixelCoord)
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
    return ComputePCF(float3(shadowUV.xy, (float)cascadeIndex), shadowUV.z, texelSize, pixelCoord);
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

    float shadow = ComputeCSMShadow(worldPos, worldNorm, viewDepth, uvInt);

    g_RWShadowMask[uvInt] = shadow;
}
