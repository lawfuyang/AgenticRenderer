// SSGI temporal reprojection pass (NormalBasic mode)
// Reprojects the raw ray march output onto the previous frame (motion
// vectors for diffuse, hit-point parallax for specular), validates the reprojection, and
// blends into the persistent accumulation in log space. Age is tracked in alpha.

#include "Common.hlsli"
#include "Bindless.hlsli"
#include "CommonLighting.hlsli"

#include "srrhi/hlsl/SSGITemporalReproject.hlsli"

static const srrhi::SSGITemporalConstants g_Temporal   = srrhi::SSGITemporalReprojectInputs::GetTemporalCB();
static const Texture2D<float4>            g_RawDiffuse       = srrhi::SSGITemporalReprojectInputs::GetRawDiffuse();
static const Texture2D<float4>            g_RawSpecular      = srrhi::SSGITemporalReprojectInputs::GetRawSpecular();
static const Texture2D<float4>            g_PrevAccumDiffuse = srrhi::SSGITemporalReprojectInputs::GetPrevAccumDiffuse();
static const Texture2D<float4>            g_PrevAccumSpecular = srrhi::SSGITemporalReprojectInputs::GetPrevAccumSpecular();
static const Texture2D<float2>            g_MotionVectors    = srrhi::SSGITemporalReprojectInputs::GetMotionVectors();
static const Texture2D<float>             g_Depth            = srrhi::SSGITemporalReprojectInputs::GetDepth();
static const Texture2D<float2>            g_GBufferNormals   = srrhi::SSGITemporalReprojectInputs::GetGBufferNormals();
static const Texture2D<float2>            g_GBufferORM       = srrhi::SSGITemporalReprojectInputs::GetGBufferORM();
static const SamplerState                 g_LinearSampler    = srrhi::SSGITemporalReprojectInputs::GetLinearSampler();
static const SamplerState                 g_PointSampler     = srrhi::SSGITemporalReprojectInputs::GetPointSampler();

// validateReprojectedUV: confidence in [0,1] that the reprojected UV points at the same surface.
// reprojUV must be in the CURRENT frame's jittered coordinate system (i.e. with TAA jitter
// cancelled out — for static scenes this equals the pixel's own UV). The caller is responsible
// for removing the jitter offset before calling this function.
float SSGIValidateReprojection(float2 reprojUV, float3 worldPos, float3 worldNormal, float2 velocityUV)
{
    if (reprojUV.x < 0.0f || reprojUV.x > 1.0f || reprojUV.y < 0.0f || reprojUV.y > 1.0f)
        return 0.0f;

    float lastDepth = g_Depth.SampleLevel(g_PointSampler, reprojUV, 0.0f);
    float2 lastVelocityUV = g_MotionVectors.SampleLevel(g_PointSampler, reprojUV, 0.0f) * g_Temporal.m_View.m_ViewportSizeInv;
    float3 lastWorldPos = ReconstructWorldPos(reprojUV, lastDepth, g_Temporal.m_View.m_MatClipToWorld);

    float viewDist = length(worldPos - g_Temporal.m_View.m_CameraDirectionOrPosition.xyz);
    float distFactor = 1.0f + 1.0f / (viewDist + 1.0f);

    float disoccl = 0.0f;
    disoccl += length(velocityUV - lastVelocityUV) / 0.005f * distFactor;          // velocity delta
    disoccl += abs(dot(worldPos - lastWorldPos, worldNormal)) / 2.5f * distFactor; // plane distance
    disoccl += length(worldPos - lastWorldPos) / 2.5f * distFactor;                // world distance
    disoccl = min(disoccl / 3.0f, 1.0f);

    return 1.0f - disoccl;
}

// Blend one channel (diffuse or specular). Returns rgb = accumulated GI, a = new age.
float4 SSGITemporalAccumulate(Texture2D<float4> accumTexture, float3 currentColor, bool bWasSampled, float2 reprojUV, float confidence, float moveFactor)
{
    float4 acc = SampleTextureCatmullRom(accumTexture, g_LinearSampler, reprojUV, g_Temporal.m_View.m_ViewportSize);

    float3 inp = currentColor;
    acc.rgb = log(acc.rgb + 1.0f);

    if (bWasSampled)
    {
        acc.a += 1.0f;
        inp = log(inp + 1.0f);
    }
    else
    {
        // not sampled this frame (sentinel): pass the accumulated value through
        inp = acc.rgb;
    }

    confidence = pow(confidence, 0.25f);

    float accumBlend = 1.0f - 1.0f / (acc.a + 1.0f);
    accumBlend = lerp(0.0f, accumBlend, confidence);

    float maxValue = lerp(1.0f, g_Temporal.m_Blend, moveFactor);
    float temporalMix = min(accumBlend, maxValue);

    float3 outputColor = lerp(inp, acc.rgb, temporalMix);
    float outputAge = 1.0f / max(1.0f - temporalMix, srrhi::CommonConsts::kEpsilon) - 1.0f;

    outputColor = exp(outputColor) - 1.0f;

    return float4(outputColor, outputAge);
}

struct SSGITemporalPSOutput
{
    float4 diffuse  : SV_Target0;   // rgb = accumulated diffuse GI, a = age
    float4 specular : SV_Target1;   // rgb = accumulated specular GI, a = age
};

SSGITemporalPSOutput SSGITemporal_PSMain(FullScreenVertexOut input)
{
    SSGITemporalPSOutput output;

    uint2 pixelPos = uint2(input.pos.xy);
    float2 uv = input.uv;

    float depth = g_Depth.SampleLevel(g_PointSampler, uv, 0.0f);

    if (depth == srrhi::CommonConsts::DEPTH_FAR)
    {
        output.diffuse = 0.0f;
        output.specular = 0.0f;
        return output;
    }

    float4 diffRaw = g_RawDiffuse.Load(int3(pixelPos, 0));
    float4 specRaw = g_RawSpecular.Load(int3(pixelPos, 0));

    bool bDiffSampled = diffRaw.r >= 0.0f; // -1 sentinel when no diffuse ray was traced
    float roughness = max(diffRaw.a, 0.0f);
    float rayLength = specRaw.a;

    float3 worldNormal = DecodeNormal(g_GBufferNormals.Load(int3(pixelPos, 0)));
    float3 worldPos = ReconstructWorldPos(uv, depth, g_Temporal.m_View.m_MatClipToWorld);
    float3 cameraPosWS = g_Temporal.m_View.m_CameraDirectionOrPosition.xyz;

    float2 velocityUV = g_MotionVectors.SampleLevel(g_PointSampler, uv, 0.0f) * g_Temporal.m_View.m_ViewportSizeInv;
    // Motion vectors point from the current to the previous position (ComputeMotionVectors
    // returns prevWindowPos - windowPos), so they are ADDED to reach the history UV.
    float2 reprojUVDiffuse = uv + velocityUV;

    // Motion vectors include the TAA jitter offset (jitter_prev - jitter_curr). The reprojected
    // UV is correct for sampling the Accum buffer (which was rendered at the previous frame's
    // jittered positions), but the VALIDATION must operate in a jitter-invariant space: if we
    // validate at the jittered reprojUV, the current frame's depth at that UV belongs to a
    // different world point (shifted by the jitter delta), causing false disocclusion at
    // geometry edges.
    //
    // Cancel the jitter from the reprojection to get a UV in the current frame's coordinate
    // system. For a static scene this equals uv itself, making the validation trivially correct.
    float2 jitterOffsetUV = (g_Temporal.m_ViewPrev.m_PixelOffset - g_Temporal.m_View.m_PixelOffset) * g_Temporal.m_View.m_ViewportSizeInv;
    float2 reprojUVNoJitterDiffuse = reprojUVDiffuse - jitterOffsetUV;

    // specular: hit-point parallax reprojection (falls back to motion vectors for
    // missed rays and rough surfaces)
    float2 reprojUVSpecular = reprojUVDiffuse;
    if (rayLength < 1.0e4f)
    {
        float3 parallaxHitPoint = cameraPosWS + normalize(worldPos - cameraPosWS) * rayLength;
        float4 clip = MatrixMultiply(float4(parallaxHitPoint, 1.0f), g_Temporal.m_ViewPrev.m_MatWorldToClip);
        float2 hitUV = ClipXYToUV(clip.xy / clip.w);

        // Blend from parallax reprojection (shiny surfaces) to motion-vector reprojection (rough surfaces).
        // Below kSSGIParallaxRoughnessThreshold, parallax is used exclusively; above it, blends toward motion vectors.
        const float kSSGIParallaxRoughnessThreshold = 0.375f;
        const float kSSGIParallaxRoughnessRange     = 0.625f;
        float roughnessFactor = saturate((roughness - kSSGIParallaxRoughnessThreshold) / kSSGIParallaxRoughnessRange);
        reprojUVSpecular = lerp(hitUV, reprojUVDiffuse, roughnessFactor);
    }
    float2 reprojUVNoJitterSpecular = reprojUVSpecular - jitterOffsetUV;

    // Validate at the jitter-invariant UV (current-frame coordinate system).
    // The Accum buffer is still sampled at the original jittered reprojUV — correct for
    // jittered history.
    float confidenceDiffuse = SSGIValidateReprojection(reprojUVNoJitterDiffuse, worldPos, worldNormal, velocityUV);
    float confidenceSpecular = SSGIValidateReprojection(reprojUVNoJitterSpecular, worldPos, worldNormal, velocityUV);

    // Motion vectors are computed from the jittered matrices, so with TAA enabled a
    // perfectly static camera still reports ~0.5 px of movement. Measuring in pixels with a
    // dead zone keeps that jitter from registering as camera motion — otherwise moveFactor
    // saturates every frame, temporalMix is capped at m_Blend, and the accumulated age can
    // never exceed 1/(1-m_Blend)-1 (~9 frames), so the estimate never converges.
    const float kSSGIMotionDeadZonePixels = 1.0f;
    float2 velocityPixels = velocityUV * g_Temporal.m_View.m_ViewportSize;
    float moveFactor = saturate(length(velocityPixels) - kSSGIMotionDeadZonePixels);

    output.diffuse = SSGITemporalAccumulate(g_PrevAccumDiffuse, diffRaw.rgb, bDiffSampled, reprojUVDiffuse, confidenceDiffuse, moveFactor);
    output.specular = SSGITemporalAccumulate(g_PrevAccumSpecular, specRaw.rgb, true, reprojUVSpecular, confidenceSpecular, moveFactor);
    return output;
}
