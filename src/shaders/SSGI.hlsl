// SSGI ray march pass (NormalBasic mode)
// View-space ray march (+Z forward, reversed-Z depth) with GGX VNDF specular sampling and optional cosine-sampled diffuse ray.
// Missed rays fall back to the atmosphere sky radiance.

#include "Common.hlsli"
#include "Bindless.hlsli"
#include "CommonLighting.hlsli"
#include "Atmosphere.hlsli"

#include "srrhi/hlsl/SSGI.hlsli"

static const srrhi::SSGIConstants g_SSGI           = srrhi::SSGIInputs::GetSSGICB();
static const Texture2D<float4>    g_GBufferAlbedo  = srrhi::SSGIInputs::GetGBufferAlbedo();
static const Texture2D<float2>    g_GBufferNormals = srrhi::SSGIInputs::GetGBufferNormals();
static const Texture2D<float2>    g_GBufferORM     = srrhi::SSGIInputs::GetGBufferORM();
static const Texture2D<float4>    g_GBufferEmissive = srrhi::SSGIInputs::GetGBufferEmissive();
static const Texture2D<float>     g_ShadowMask     = srrhi::SSGIInputs::GetShadowMask();
static const Texture2D<float>     g_Depth          = srrhi::SSGIInputs::GetDepth();
static const Texture2D<float2>    g_MotionVectors  = srrhi::SSGIInputs::GetMotionVectors();
static const Texture2D<float4>    g_AccumDiffuse   = srrhi::SSGIInputs::GetAccumDiffuse();
static const Texture2D<float4>    g_AccumSpecular  = srrhi::SSGIInputs::GetAccumSpecular();
static const Texture2D<float4>    g_BlueNoise      = srrhi::SSGIInputs::GetBlueNoise();
static const SamplerState         g_LinearSampler  = srrhi::SSGIInputs::GetLinearSampler();
static const SamplerState         g_PointSampler   = srrhi::SSGIInputs::GetPointSampler();

#define SSGI_INVALID_RAY_COORDS float2(-1.0f, -1.0f)

// Binary search refinement after initial hit: pinpoints the exact intersection.
float2 SSGIBinarySearch(inout float3 dir, inout float3 hitPos, float4x4 matViewToClip)
{
    float2 uv = 0.0f;
    float clipW;

    dir *= 0.5f;
    hitPos -= dir;

    for (uint i = 0; i < g_SSGI.m_RefineSteps; i++)
    {
        uv = ProjectViewToUV(hitPos, matViewToClip, clipW);

        float sceneDepth = g_Depth.SampleLevel(g_PointSampler, uv, 0.0f);
        float sceneLinZ = ConvertToLinearDepth(sceneDepth, g_SSGI.m_View.m_MatViewToClip);

        // +Z forward: diff > 0 means the marched point is behind the scene surface
        float diff = hitPos.z - sceneLinZ;

        dir *= 0.5f;
        hitPos += (diff > 0.0f) ? -dir : dir;
    }

    uv = ProjectViewToUV(hitPos, matViewToClip, clipW);
    return uv;
}

// dir: view-space ray direction (normalized). hitPos: view-space start position (already
// offset along the surface normal by the caller to avoid immediate self-intersection).
// bOccluded: set when the march passed behind scene geometry without accepting it as a hit.
// The direction is then known to be blocked, so a miss must not fall back to sky radiance.
float2 SSGIRayMarch(inout float3 dir, inout float3 hitPos, float jitter, out bool bOccluded)
{
    dir *= g_SSGI.m_RayDistance / float(g_SSGI.m_Steps);

    hitPos += dir * jitter * 3.0f;

    float2 uv = 0.0f;
    float clipW;

    // The ray leaves its surface into free space, so it starts in front of the depth buffer.
    bool bWasInFront = true;
    bOccluded = false;

    for (uint i = 1; i < g_SSGI.m_Steps; i++)
    {
        // use slower increments for the first few steps to sharpen contact shadows
        float m = exp(pow(float(i) / 4.0f, 0.05f)) - 2.0f;
        float3 stepVec = dir * min(m, 1.0f);
        hitPos += stepVec;

        if (hitPos.z <= 0.0f)
            return SSGI_INVALID_RAY_COORDS; // behind the camera

        uv = ProjectViewToUV(hitPos, g_SSGI.m_View.m_MatViewToClip, clipW);

        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            return SSGI_INVALID_RAY_COORDS; // left the screen

        float sceneDepth = g_Depth.SampleLevel(g_PointSampler, uv, 0.0f);
        float sceneLinZ = ConvertToLinearDepth(sceneDepth, g_SSGI.m_View.m_MatViewToClip);

        float diff = hitPos.z - sceneLinZ;

        if (diff < 0.0f)
        {
            bWasInFront = true; // in front of the depth buffer surface — arm the crossing test
            continue;
        }

        // Behind the surface. Subtract the depth this step advanced: what remains is how far the
        // ray overshot the surface *beyond the sampling granularity*, which makes the tolerance
        // test independent of step size.
        //
        // Testing raw `diff < m_Thickness` instead is what let light leak through walls: the step
        // advances m_RayDistance/m_Steps (0.5 world units by default) in depth, so a crossing can
        // jump straight over a [0, 0.5) acceptance window in a single step. The crossing is then
        // missed, the ray keeps marching THROUGH the occluder, and it reports a hit on whatever
        // bright geometry sits behind it — a surface with no line of sight to the sunlit wall
        // gets that wall's radiance anyway.
        float overshoot = diff - max(stepVec.z, 0.0f);

        if (bWasInFront && overshoot < g_SSGI.m_Thickness)
        {
            if (g_SSGI.m_RefineSteps > 0)
                return SSGIBinarySearch(dir, hitPos, g_SSGI.m_View.m_MatViewToClip);
            return uv;
        }

        // Crossed behind geometry but too deep to call it an intersection (the ray is passing
        // behind something far in front of it). The direction is still blocked by that geometry.
        bWasInFront = false;
        bOccluded = true;
    }

    return SSGI_INVALID_RAY_COORDS;
}

// Radiance seen along the ray: direct lighting at the hit point plus the reprojected
// previous-frame GI (multi-bounce feedback), atmosphere sky radiance on a miss.
float3 SSGISampleHitRadiance(float2 coords, bool bIsMissedRay, bool bOccluded, float3 lView, float3 worldNormal, float roughness,
                             bool bIsDiffuseSample, float3 sunRadiance)
{
    float3 cameraPosWS = g_SSGI.m_View.m_CameraDirectionOrPosition.xyz;
    float3 rayDirWS = normalize(MatrixMultiply(lView, (float3x3)g_SSGI.m_View.m_MatViewToWorld));
    float3 skyColor = GetAtmosphereSkyRadiance(cameraPosWS, rayDirWS, g_SSGI.m_SunDirection, g_SSGI.m_SunIntensity, /*bAddSunDisk=*/false);

    if (bIsMissedRay)
    {
        // A miss only means "no usable hit in the depth buffer", not "open sky". If the march
        // passed behind geometry on its way out, that geometry blocks the direction and the sky
        // is not visible along it — handing back full sky radiance is what makes enclosed
        // interiors glow with skylight they cannot possibly receive.
        return bOccluded ? float3(0.0f, 0.0f, 0.0f) : skyColor;
    }

    float3 hitNormal = DecodeNormal(g_GBufferNormals.SampleLevel(g_PointSampler, coords, 0.0f));

    // Coplanar hit: a ray leaving a surface can never legitimately land on a surface with the
    // same normal orientation, so this is a grazing self-intersection artefact of the march.
    // Reject the sample (there IS geometry there, so the sky is definitely not visible — the
    // previous sky fallback here was injecting full skylight into every large flat wall).
    if (dot(worldNormal, hitNormal) > 0.999f)
        return float3(0.0f, 0.0f, 0.0f);

    // Direct lighting emitted by the hit surface — this is what actually injects energy
    // into the GI feedback loop (the accumulation buffers alone are a zero fixed point).
    float3 hitAlbedo    = g_GBufferAlbedo.SampleLevel(g_PointSampler, coords, 0.0f).rgb;
    float  hitMetalness = g_GBufferORM.SampleLevel(g_PointSampler, coords, 0.0f).g;
    float  hitShadow    = g_ShadowMask.SampleLevel(g_PointSampler, coords, 0.0f);
    float3 hitEmissive  = g_GBufferEmissive.SampleLevel(g_PointSampler, coords, 0.0f).rgb;
    float3 directLight  = hitAlbedo * (1.0f - hitMetalness) * Lambert(hitNormal, g_SSGI.m_SunDirection) * hitShadow * sunRadiance + hitEmissive;

    // reproject the hit coords into the previous frame.
    // Motion vectors are in pixels and point from the current to the previous position
    // (ComputeMotionVectors returns prevWindowPos - windowPos), so they are ADDED.
    float2 velocity = g_MotionVectors.SampleLevel(g_PointSampler, coords, 0.0f);
    float2 reprojectedUV = coords + velocity * g_SSGI.m_View.m_ViewportSizeInv;

    if (reprojectedUV.x < 0.0f || reprojectedUV.x > 1.0f || reprojectedUV.y < 0.0f || reprojectedUV.y > 1.0f)
        return directLight;

    float4 reprojectedGI = bIsDiffuseSample
        ? g_AccumDiffuse.SampleLevel(g_LinearSampler, reprojectedUV, 0.0f)
        : g_AccumSpecular.SampleLevel(g_LinearSampler, reprojectedUV, 0.0f);

    float pixelAge = reprojectedGI.a;

    // desaturate young (noisy) pixels to hide temporal noise.
    // saturation maps [0,1] to [kSSGIDesatMinColor, 1.0] so brand-new pixels
    // retain at least kSSGIDesatMinColor of their original hue (never fully grayscale).
    const float kSSGIDesatMinColor = 0.25f;
    const float kSSGIDesatRange    = 0.75f;
    float saturation = lerp(1.0f / (pixelAge + 1.0f), 1.0f, roughness);
    reprojectedGI.rgb = lerp(Luminance(reprojectedGI.rgb).xxx, reprojectedGI.rgb, kSSGIDesatMinColor + saturation * kSSGIDesatRange);

    // fade out near screen edges (source: https://imanolfotia.com/blog/1)
    // UV distance from center where fade begins (inner safe zone) / completes (beyond screen, giving a soft vignette at borders). Reprojection is unreliable near edges.
    const float kSSGIScreenEdgeFadeStart = 0.2f;
    const float kSSGIScreenEdgeFadeEnd   = 0.6f;
    float2 dCoords = smoothstep(kSSGIScreenEdgeFadeStart, kSSGIScreenEdgeFadeEnd, abs(float2(0.5f, 0.5f) - coords));
    float screenEdgeFactor = saturate(1.0f - (dCoords.x + dCoords.y));

    return directLight + reprojectedGI.rgb * screenEdgeFactor;
}

float3 SSGITraceRay(float3 l, float3 viewPos, float jitter, float3 worldNormal, float roughness, bool bIsDiffuseSample,
                    float3 sunRadiance, out bool bIsMissedRay, out float3 outHitPos)
{
    float3 dir = l;
    float3 hitPos = viewPos;

    bool bOccluded;
    float2 coords = SSGIRayMarch(dir, hitPos, jitter, bOccluded);

    bIsMissedRay = coords.x < 0.0f;
    outHitPos = hitPos;

    float3 radiance = SSGISampleHitRadiance(coords, bIsMissedRay, bOccluded, l, worldNormal, roughness, bIsDiffuseSample, sunRadiance);
    return radiance;
}

struct SSGIPSOutput
{
    float4 diffuse  : SV_Target0;   // rgb = diffuse GI (or -1 sentinel), a = roughness
    float4 specular : SV_Target1;   // rgb = specular GI, a = ray length
};

SSGIPSOutput SSGI_PSMain(FullScreenVertexOut input)
{
    SSGIPSOutput output;

    uint2 pixelPos = uint2(input.pos.xy);
    float2 uv = input.uv;

    float depth = g_Depth.SampleLevel(g_PointSampler, uv, 0.0f);

    // sky / far plane
    if (depth == srrhi::CommonConsts::DEPTH_FAR)
    {
        output.diffuse  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        output.specular = float4(0.0f, 0.0f, 0.0f, 10.0e4f);
        return output;
    }

    float4 albedoAlpha = g_GBufferAlbedo.Load(int3(pixelPos, 0));
    float3 albedo = albedoAlpha.rgb;
    float3 worldNormal = DecodeNormal(g_GBufferNormals.Load(int3(pixelPos, 0)));
    float2 orm = g_GBufferORM.Load(int3(pixelPos, 0));
    float roughness = orm.r;
    float metalness = orm.g;

    float3 cameraPosWS = g_SSGI.m_View.m_CameraDirectionOrPosition.xyz;

    // view-space position of the current texel (+Z forward)
    float2 clipXY = UVToClipXY(uv);
    float4 viewH = MatrixMultiply(float4(clipXY, depth, 1.0f), g_SSGI.m_View.m_MatClipToView);
    float3 viewPos = viewH.xyz / viewH.w;

    float3 viewDir = normalize(viewPos);
    float3 viewNormal = normalize(MatrixMultiply(worldNormal, (float3x3)g_SSGI.m_View.m_MatWorldToView));

    float3 n = viewNormal;
    float3 v = -viewDir;
    float NoV = max(srrhi::CommonConsts::kEpsilon, dot(n, v));

    float4 random = SampleBlueNoise(g_BlueNoise, pixelPos, g_SSGI.m_Frame);

    // Ray origin pushed off the surface along the view-space normal. Without this the march
    // starts exactly on the surface and grazing rays immediately report a hit against the
    // originating texel (the depth test only needs to be within m_Thickness behind it).
    const float kSSGIRayOriginNormalBias = 0.02f;
    float3 rayOrigin = viewPos + viewNormal * (kSSGIRayOriginNormalBias * (1.0f + viewPos.z));

    // Direct sun radiance at this pixel's altitude — reused as the incoming sun radiance for
    // every ray hit (transmittance variation over the SSGI ray range is negligible).
    float3 worldPos = MatrixMultiply(float4(viewPos, 1.0f), g_SSGI.m_View.m_MatViewToWorld).xyz;
    float3 sunRadiance = GetAtmosphereSunRadiance(GetAtmospherePos(worldPos), g_SSGI.m_SunDirection, g_SSGI.m_SunIntensity);

    // GGX reflection ray, sampled around the world-space normal
    float3 T, B;
    BuildTangentFrame(worldNormal, T, B);

    float3 Vworld = normalize(MatrixMultiply(v, (float3x3)g_SSGI.m_View.m_MatViewToWorld));
    float3 Vlocal = TangentToLocal(T, B, worldNormal, Vworld);

    float3 H = sampleGGX_VNDF(Vlocal, roughness, random.rg);
    if (H.z < 0.0f)
        H = -H;

    float3 lLocal = normalize(reflect(-Vlocal, H));
    float3 lWorld = TangentToWorld(T, B, worldNormal, lLocal);
    float3 l = normalize(MatrixMultiply(lWorld, (float3x3)g_SSGI.m_View.m_MatWorldToView)); // view-space specular ray

    float3 h = normalize(v + l);
    float NoL = clamp(dot(n, l), srrhi::CommonConsts::kEpsilon, 1.0f - srrhi::CommonConsts::kEpsilon);
    float NoH = clamp(dot(n, h), srrhi::CommonConsts::kEpsilon, 1.0f - srrhi::CommonConsts::kEpsilon);
    float VoH = clamp(dot(v, h), srrhi::CommonConsts::kEpsilon, 1.0f - srrhi::CommonConsts::kEpsilon);

    // fresnel f0 and diffuse/specular trace weights
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
    float3 F = Schlick_Fresnel(f0, VoH);

    float diffW = max((1.0f - metalness) * Luminance(albedo), srrhi::CommonConsts::kEpsilon);
    float specW = max(Luminance(F), srrhi::CommonConsts::kEpsilon);
    diffW /= (diffW + specW);

    bool bIsDiffuseSample = random.b < diffW;

    // ---- optional diffuse ray (cosine-sampled hemisphere) ----
    float3 diffuseGI = float3(-1.0f, -1.0f, -1.0f);
    if (bIsDiffuseSample)
    {
        float3 diffuseRay = SampleHemisphereCosine(random.rg, viewNormal);

        float3 hd = normalize(v + diffuseRay);
        float NoLd = clamp(dot(n, diffuseRay), srrhi::CommonConsts::kEpsilon, 1.0f - srrhi::CommonConsts::kEpsilon);
        float LoHd = clamp(dot(diffuseRay, hd), srrhi::CommonConsts::kEpsilon, 1.0f - srrhi::CommonConsts::kEpsilon);

        // DisneyBurleyDiffuse returns Fd * NoL / PI
        // with NoL folded in afterwards — identical result after dividing by pdf = NoL / PI.
        float brdf = DisneyBurleyDiffuse(NoLd, NoV, LoHd, roughness) * (1.0f - metalness);
        float pdf = max(NoLd / srrhi::CommonConsts::PI, srrhi::CommonConsts::kEpsilon);

        bool bIsMissedRayD;
        float3 hitPosD;
        // jitter uses random.a: random.b already decided diffuse-vs-specular, so reusing it
        // would correlate the march start offset with the sample type (diffuse samples would
        // always get a small offset, specular samples a large one).
        float3 radianceD = SSGITraceRay(diffuseRay, rayOrigin, random.a, worldNormal, roughness, true, sunRadiance, bIsMissedRayD, hitPosD);

        diffuseGI = radianceD * brdf / pdf;
    }

    // ---- specular ray (traced every frame) ----
    // BRDF * NoL without the Fresnel term (F is applied later in the compose pass):
    // D * G2/NoV * 0.25, matching GGXTimesNdotL_Exact minus F.
    float brdfNdotL = D_GGX(NoH, roughness) * G_SmithOverNdotV_Exact(roughness, NoV, NoL) * 0.25f;
    float pdfS = max(GGXVNDFPdf(NoH, NoV, roughness), srrhi::CommonConsts::kEpsilon);

    bool bIsMissedRayS;
    float3 hitPosS;
    float3 radianceS = SSGITraceRay(l, rayOrigin, random.a, worldNormal, roughness, false, sunRadiance, bIsMissedRayS, hitPosS);

    float3 specularGI = radianceS * brdfNdotL / pdfS;

    // world-space ray length of the specular ray, used for hit-point parallax
    // reprojection in the temporal pass
    float rayLength;
    if (bIsMissedRayS)
    {
        rayLength = 10.0e4f;
    }
    else
    {
        float3 hitPosWS = MatrixMultiply(float4(hitPosS, 1.0f), g_SSGI.m_View.m_MatViewToWorld).xyz;
        rayLength = distance(cameraPosWS, hitPosWS);
    }

    output.diffuse = float4(diffuseGI, roughness);
    output.specular = float4(specularGI, rayLength);
    return output;
}
