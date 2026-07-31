// SSGI compose pass (NormalBasic mode)
// Applies the surface BRDF to the denoised GI:
// deterministic VNDF half-vector for a stable Fresnel estimate, diffuse modulated by
// albedo * (1 - F), specular modulated by F. The output is the pure indirect lighting
// term consumed by DeferredRenderer (emissive is added there — not here — to avoid
// double-counting).
//
// When m_DebugMode != 0 this pass instead outputs an SSGI diagnostic visualization and
// DeferredLighting replaces the pixel colour with it instead of adding it.

#include "Common.hlsli"
#include "Bindless.hlsli"
#include "CommonLighting.hlsli"
#include "Atmosphere.hlsli"
#include "SSGICommon.hlsli"

#include "srrhi/hlsl/SSGICompose.hlsli"

static const srrhi::SSGIComposeConstants g_Compose   = srrhi::SSGIComposeInputs::GetComposeCB();
static const Texture2D<float4>           g_DenoisedDiffuse  = srrhi::SSGIComposeInputs::GetDenoisedDiffuse();
static const Texture2D<float4>           g_DenoisedSpecular = srrhi::SSGIComposeInputs::GetDenoisedSpecular();
static const Texture2D<float4>           g_GBufferAlbedo    = srrhi::SSGIComposeInputs::GetGBufferAlbedo();
static const Texture2D<float2>           g_GBufferNormals   = srrhi::SSGIComposeInputs::GetGBufferNormals();
static const Texture2D<float2>           g_GBufferORM       = srrhi::SSGIComposeInputs::GetGBufferORM();
static const Texture2D<float4>           g_GBufferEmissive  = srrhi::SSGIComposeInputs::GetGBufferEmissive();
static const Texture2D<float>            g_ShadowMask       = srrhi::SSGIComposeInputs::GetShadowMask();
static const Texture2D<float>            g_Depth            = srrhi::SSGIComposeInputs::GetDepth();
static const Texture2D<float4>           g_RawDiffuse       = srrhi::SSGIComposeInputs::GetRawDiffuse();
static const Texture2D<float4>           g_RawSpecular      = srrhi::SSGIComposeInputs::GetRawSpecular();
static const Texture2D<float4>           g_TemporalDiffuse  = srrhi::SSGIComposeInputs::GetTemporalDiffuse();
static const Texture2D<float4>           g_TemporalSpecular = srrhi::SSGIComposeInputs::GetTemporalSpecular();
static const SamplerState                g_PointSampler     = srrhi::SSGIComposeInputs::GetPointSampler();

// Age heatmap: black (fresh) -> red -> yellow -> white (converged).
// Log scale, because the interesting range is compressed at the bottom: while the camera
// moves the age is capped at 1/(1-m_Blend)-1 (~9 frames), while a static camera accumulates
// into the hundreds.
float3 SSGIDebugAgeHeatmap(float age)
{
    float t = saturate(log2(age + 1.0f) / 10.0f);   // 0 .. 1023 frames
    return float3(saturate(t * 3.0f), saturate(t * 3.0f - 1.0f), saturate(t * 3.0f - 2.0f));
}

// Magenta = NaN/Inf anywhere in the chain, yellow = negative radiance, grey = healthy.
float3 SSGIDebugValidity(float4 values[4])
{
    bool bIsBad = false;
    bool bIsNegative = false;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        // isnan/isinf on the raw channels; the -1 diffuse sentinel is excluded from the
        // negative test by only flagging channels below -1.
        bIsBad      = bIsBad || any(isnan(values[i])) || any(isinf(values[i]));
        bIsNegative = bIsNegative || any(values[i].rgb < -1.0f);
    }

    if (bIsBad)
        return float3(1.0f, 0.0f, 1.0f);
    if (bIsNegative)
        return float3(1.0f, 1.0f, 0.0f);
    return float3(0.25f, 0.25f, 0.25f);
}

float4 SSGICompose_PSMain(FullScreenVertexOut input) : SV_Target
{
    uint2 pixelPos = uint2(input.pos.xy);
    float2 uv = input.uv;

    float depth = g_Depth.Load(int3(pixelPos, 0));

    if (depth == srrhi::CommonConsts::DEPTH_FAR)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float3 albedo = g_GBufferAlbedo.Load(int3(pixelPos, 0)).rgb;
    float3 normal = DecodeNormal(g_GBufferNormals.Load(int3(pixelPos, 0)));
    float2 orm = g_GBufferORM.Load(int3(pixelPos, 0));
    float roughness = orm.r;
    float metalness = orm.g;

    float3 worldPos = ReconstructWorldPos(uv, depth, g_Compose.m_View.m_MatClipToWorld);
    float3 V = normalize(g_Compose.m_View.m_CameraDirectionOrPosition.xyz - worldPos);

    // deterministic VNDF half-vector sample for a stable Fresnel term
    float3 T, B;
    BuildTangentFrame(normal, T, B);
    float3 Vlocal = TangentToLocal(T, B, normal, V);

    float3 H = sampleGGX_VNDF(Vlocal, roughness, float2(0.25f, 0.25f));
    if (H.z < 0.0f)
        H = -H;

    float3 lLocal = normalize(reflect(-Vlocal, H));
    float3 l = TangentToWorld(T, B, normal, lLocal);

    float3 h = normalize(V + l);
    float VoH = max(srrhi::CommonConsts::kEpsilon, dot(V, h));

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
    float3 F = Schlick_Fresnel(f0, VoH);

    float4 denoisedDiffuse = g_DenoisedDiffuse.Load(int3(pixelPos, 0));
    float4 denoisedSpecular = g_DenoisedSpecular.Load(int3(pixelPos, 0));

    float3 diffuseComponent = albedo * (1.0f - metalness) * (1.0f - F) * denoisedDiffuse.rgb;
    float3 specularComponent = F * denoisedSpecular.rgb;
    float3 composed = diffuseComponent + specularComponent;

    if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_OFF)
        return float4(composed, 1.0f);

    // ── Debug visualizations ───────────────────────────────────────────────
    float4 rawDiffuse = g_RawDiffuse.Load(int3(pixelPos, 0));
    float4 rawSpecular = g_RawSpecular.Load(int3(pixelPos, 0));
    float4 temporalDiffuse = g_TemporalDiffuse.Load(int3(pixelPos, 0));
    float4 temporalSpecular = g_TemporalSpecular.Load(int3(pixelPos, 0));

    bool bIsMissedRay = rawSpecular.a >= 1.0e4f;
    bool bHadDiffuseRay = rawDiffuse.r >= 0.0f;

    float3 debug = float3(0.0f, 0.0f, 0.0f);

    if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_RAW_DIFFUSE)
        debug = max(rawDiffuse.rgb, 0.0f);                    // clamps the -1 sentinel to black
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_RAW_SPECULAR)
        debug = rawSpecular.rgb;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_TEMPORAL_DIFFUSE)
        debug = temporalDiffuse.rgb;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_TEMPORAL_SPECULAR)
        debug = temporalSpecular.rgb;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_DENOISED_DIFFUSE)
        debug = denoisedDiffuse.rgb;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_DENOISED_SPECULAR)
        debug = denoisedSpecular.rgb;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_COMPOSED)
        debug = composed;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_DIFFUSE_AGE)
        debug = SSGIDebugAgeHeatmap(denoisedDiffuse.a);
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_SPECULAR_AGE)
        debug = SSGIDebugAgeHeatmap(denoisedSpecular.a);
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_RAY_LENGTH)
        debug = bIsMissedRay ? float3(1.0f, 0.0f, 1.0f) : saturate(rawSpecular.a / 25.0f).xxx;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_HIT_MASK)
        debug = bIsMissedRay ? float3(0.6f, 0.0f, 0.0f) : float3(0.0f, 0.6f, 0.0f);
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_SAMPLE_TYPE)
        debug = bHadDiffuseRay ? float3(0.0f, 0.2f, 0.8f) : float3(0.8f, 0.2f, 0.0f);
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_SKY_FALLBACK)
    {
        // Sky radiance the ray march would return for a missed mirror ray at this pixel.
        // Black here means the atmosphere lookup contributes nothing to SSGI.
        debug = GetAtmosphereSkyRadiance(g_Compose.m_View.m_CameraDirectionOrPosition.xyz, l,
                                        g_Compose.m_SunDirection, g_Compose.m_SunIntensity, /*bAddSunDisk=*/false);
    }
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_DIRECT_LIGHT)
    {
        // The screen-space direct light that gets injected at ray hits. Black here means
        // the GI feedback loop has no energy source and the result can only ever be black.
        float3 sunRadiance = GetAtmosphereSunRadiance(GetAtmospherePos(worldPos), g_Compose.m_SunDirection, g_Compose.m_SunIntensity);
        debug = SSGIScreenDirectLight(g_GBufferAlbedo, g_GBufferNormals, g_GBufferORM, g_GBufferEmissive,
                                      g_ShadowMask, g_PointSampler, uv, g_Compose.m_SunDirection, sunRadiance);
    }
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_FRESNEL)
        debug = F;
    else if (g_Compose.m_DebugMode == srrhi::SSGIDebugMode::SSGI_DEBUG_VALIDITY)
    {
        float4 stages[4] = { rawDiffuse, rawSpecular, temporalDiffuse, denoisedDiffuse };
        debug = SSGIDebugValidity(stages);
    }

    return float4(debug, 1.0f);
}
