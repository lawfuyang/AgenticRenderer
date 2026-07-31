// SSGI Poisson denoise pass (NormalBasic mode)
// 8-tap rotated Poisson disk joint bilateral filter over the temporally accumulated GI.
// Filtering happens in log space; age (alpha) is preserved. The denoised result is written
// into the accum ping-pong write slot and becomes next frame's reprojection source.

#include "Common.hlsli"
#include "Bindless.hlsli"
#include "CommonLighting.hlsli"

#include "srrhi/hlsl/SSGIDenoise.hlsli"

static const srrhi::SSGIDenoiseConstants g_Denoise     = srrhi::SSGIDenoiseInputs::GetDenoiseCB();
static const Texture2D<float4>           g_InputDiffuse  = srrhi::SSGIDenoiseInputs::GetInputDiffuse();
static const Texture2D<float4>           g_InputSpecular = srrhi::SSGIDenoiseInputs::GetInputSpecular();
static const Texture2D<float>            g_Depth         = srrhi::SSGIDenoiseInputs::GetDepth();
static const Texture2D<float2>           g_GBufferNormals = srrhi::SSGIDenoiseInputs::GetGBufferNormals();
static const Texture2D<float2>           g_GBufferORM    = srrhi::SSGIDenoiseInputs::GetGBufferORM();
static const Texture2D<float4>           g_BlueNoise     = srrhi::SSGIDenoiseInputs::GetBlueNoise();
static const SamplerState                g_PointSampler  = srrhi::SSGIDenoiseInputs::GetPointSampler();

// luminance variant used by the denoiser
float SSGIDenoiseLuminance(float3 c)
{
    return pow(Luminance(c), 0.125f);
}

float3 SSGIToDenoiseSpace(float3 c) { return log(c + 1.0f); }
float3 SSGIToLinearSpace(float3 c)  { return exp(c) - 1.0f; }

// by Nvidia ReBLUR: 4 cardinal taps at distance 1, 4 diagonal taps at distance 0.5
static const float2 kPoissonDisk[8] =
{
    float2(-1.0f, 0.0f), float2(0.0f, -1.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
    float2(-0.353553f, -0.353553f), float2(0.353553f, -0.353553f),
    float2(0.353553f, 0.353553f), float2(-0.353553f, 0.353553f)
};

struct SSGIDenoisePSOutput
{
    float4 diffuse  : SV_Target0;   // rgb = denoised diffuse GI, a = age
    float4 specular : SV_Target1;   // rgb = denoised specular GI, a = age
};

SSGIDenoisePSOutput SSGIDenoise_PSMain(FullScreenVertexOut input)
{
    SSGIDenoisePSOutput output;

    uint2 pixelPos = uint2(input.pos.xy);
    float2 uv = input.uv;

    float centerDepth = g_Depth.SampleLevel(g_PointSampler, uv, 0.0f);

    if (centerDepth == srrhi::CommonConsts::DEPTH_FAR)
    {
        output.diffuse = 0.0f;
        output.specular = 0.0f;
        return output;
    }

    float4 centerDiffuseGi = g_InputDiffuse.Load(int3(pixelPos, 0));
    float4 centerSpecularGi = g_InputSpecular.Load(int3(pixelPos, 0));

    // Age drives how aggressively the pixel is filtered (old = converged = leave alone), but
    // it is clamped rather than used raw. With a static camera the temporal age grows without
    // bound, so the unclamped falloffs below (1/sqrt(age) on the tap weights, exp(-age) on the
    // kernel radius) drove the filter to exactly zero — the pass became a bit-exact copy and
    // any residual variance in the converged estimate was left on screen forever. Clamping
    // makes the filter strength saturate at a mild-but-non-zero level instead.
    const float kSSGIDenoiseMaxEffectiveAge = 64.0f;
    float outputAge = centerDiffuseGi.a;
    float outputAge2 = centerSpecularGi.a;
    float a = min(outputAge, kSSGIDenoiseMaxEffectiveAge);
    float a2 = min(outputAge2, kSSGIDenoiseMaxEffectiveAge);

    // the weights w, w2 make the denoiser more aggressive the younger the pixel is
    float w = 1.0f / sqrt(a + 1.0f);
    float w2 = 1.0f / sqrt(a2 + 1.0f);

    centerDiffuseGi.rgb = SSGIToDenoiseSpace(centerDiffuseGi.rgb);
    centerSpecularGi.rgb = SSGIToDenoiseSpace(centerSpecularGi.rgb);

    float centerLumDiffuse = SSGIDenoiseLuminance(centerDiffuseGi.rgb);
    float centerLumSpecular = SSGIDenoiseLuminance(centerSpecularGi.rgb);

    float3 normal = DecodeNormal(g_GBufferNormals.Load(int3(pixelPos, 0)));
    float2 orm = g_GBufferORM.Load(int3(pixelPos, 0));
    float centerRoughness = orm.r;
    float centerMetalness = orm.g;

    float specularFactor = (1.0f - centerRoughness) * (1.0f - centerRoughness);

    float3 centerWorldPos = ReconstructWorldPos(uv, centerDepth, g_Denoise.m_View.m_MatClipToWorld);
    float distanceToCamera = length(centerWorldPos - g_Denoise.m_View.m_CameraDirectionOrPosition.xyz);

    float roughnessRadius = lerp(sqrt(centerRoughness), 1.0f, 0.5f * (1.0f - centerMetalness));

    float4 random = SampleBlueNoise(g_BlueNoise, pixelPos, g_Denoise.m_Frame);

    // Converged pixels need less spatial filtering, but the falloff is floored: at
    // a + a2 == 300 the raw exp() is already 0.05, which drives the kernel below one texel.
    const float kSSGIDenoiseMinAgeFalloff = 0.15f;
    float ageFalloff = max(exp(-(a + a2) * 0.01f), kSSGIDenoiseMinAgeFalloff);
    float r = sqrt(random.a) * ageFalloff * g_Denoise.m_Radius * roughnessRadius;

    // rotate the poisson disk
    float angle = random.r * 2.0f * srrhi::CommonConsts::PI;
    float s, c;
    sincos(angle, s, c);

    // Kernel radius in texels. The perspective term keeps the filter footprint roughly
    // constant in world space (distant surfaces get a proportionally smaller screen-space kernel)
    //
    // The clamp is essential: kPoissonDisk's diagonal taps sit at 0.5x the radius, so a
    // diskScale below 2 texels makes them (and below 1, all of them) land back on the centre
    // texel under point sampling — the whole pass then becomes an exact pass-through, which
    // is why the denoised output was pixel-identical to the temporal output. The upper bound
    // stops the kernel from becoming a screen-wide blur on geometry very close to the camera.
    const float kSSGIDenoisePerspectiveScale = 25.0f;
    const float kSSGIDenoiseMinKernelTexels  = 2.0f;
    float diskScale = r * kSSGIDenoisePerspectiveScale / distanceToCamera;
    diskScale = clamp(diskScale, kSSGIDenoiseMinKernelTexels, g_Denoise.m_Radius * 4.0f);

    float3 denoisedDiffuse = centerDiffuseGi.rgb;
    float3 denoisedSpecular = centerSpecularGi.rgb;
    float totalWeight = 1.0f;
    float totalWeight2 = 1.0f;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 rotated = float2(kPoissonDisk[i].x * c - kPoissonDisk[i].y * s,
                                kPoissonDisk[i].x * s + kPoissonDisk[i].y * c);
        float2 neighborUv = uv + rotated * diskScale * g_Denoise.m_View.m_ViewportSizeInv;

        float neighborDepth = g_Depth.SampleLevel(g_PointSampler, neighborUv, 0.0f);
        if (neighborDepth == srrhi::CommonConsts::DEPTH_FAR)
            continue;

        float4 neighborDiffuseGi = g_InputDiffuse.SampleLevel(g_PointSampler, neighborUv, 0.0f);
        float4 neighborSpecularGi = g_InputSpecular.SampleLevel(g_PointSampler, neighborUv, 0.0f);

        neighborDiffuseGi.rgb = SSGIToDenoiseSpace(neighborDiffuseGi.rgb);
        neighborSpecularGi.rgb = SSGIToDenoiseSpace(neighborSpecularGi.rgb);

        float neighborLumDiffuse = SSGIDenoiseLuminance(neighborDiffuseGi.rgb);
        float neighborLumSpecular = SSGIDenoiseLuminance(neighborSpecularGi.rgb);

        float3 neighborNormal = DecodeNormal(g_GBufferNormals.SampleLevel(g_PointSampler, neighborUv, 0.0f));
        float neighborRoughness = g_GBufferORM.SampleLevel(g_PointSampler, neighborUv, 0.0f).r;
        float3 neighborWorldPos = ReconstructWorldPos(neighborUv, neighborDepth, g_Denoise.m_View.m_MatClipToWorld);

        float normalDiff = 1.0f - max(dot(normal, neighborNormal), 0.0f);
        float depthDiff = 10.0f * abs(dot(centerWorldPos - neighborWorldPos, normal)); // plane distance
        float roughnessDiff = abs(centerRoughness - neighborRoughness);

        float lumaDiff = lerp(abs(centerLumDiffuse - neighborLumDiffuse), 0.0f, w);
        float lumaDiff2 = lerp(abs(centerLumSpecular - neighborLumSpecular), 0.0f, w2);

        float wBasic = exp(-normalDiff * g_Denoise.m_NormalPhi - depthDiff * g_Denoise.m_DepthPhi
                           - roughnessDiff * g_Denoise.m_RoughnessPhi);

        // diffuse weight
        // Relax the normal edge-stop for young pixels (high w): at 10 vs the user-set
        // m_NormalPhi (~50), exp(-normalDiff*10) decays slower, allowing more neighbors
        // to contribute to the noisy young pixel. Old pixels stay strict to preserve detail.
        const float kSSGIDenoiseYoungNormalPhi = 10.0f;
        float wBasicD = lerp(wBasic, exp(-normalDiff * kSSGIDenoiseYoungNormalPhi), w);
        float wDiff = w * pow(wBasicD * exp(-lumaDiff * g_Denoise.m_LumaPhi), g_Denoise.m_Phi / w);
        wDiff = min(wDiff, 1.0f);

        denoisedDiffuse += wDiff * neighborDiffuseGi.rgb;
        totalWeight += wDiff;

        // specular weight (same normal-phi relaxation as diffuse)
        float wBasicS = lerp(wBasic, exp(-normalDiff * kSSGIDenoiseYoungNormalPhi), w2);
        float wSpec = w2 * pow(wBasicS * exp(-lumaDiff2 * g_Denoise.m_LumaPhi), g_Denoise.m_Phi / w2);
        wSpec = pow(wSpec, 1.0f + g_Denoise.m_SpecularPhi * specularFactor);
        wSpec = min(wSpec, 1.0f);

        denoisedSpecular += wSpec * neighborSpecularGi.rgb;
        totalWeight2 += wSpec;
    }

    denoisedDiffuse /= totalWeight;
    denoisedSpecular /= totalWeight2;

    // The unclamped age is passed through so the temporal accumulator keeps converging.
    output.diffuse = float4(SSGIToLinearSpace(denoisedDiffuse), outputAge);
    output.specular = float4(SSGIToLinearSpace(denoisedSpecular), outputAge2);
    return output;
}
