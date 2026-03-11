// ============================================================================
// SECTION 1: Boiling filter defines (for RTXDITemporalResampling)
// ============================================================================
#define RTXDI_ENABLE_BOILING_FILTER    1
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8
#define RTXDI_PRESAMPLING_GROUP_SIZE    256

// ============================================================================
// SECTION 2: Common includes
// ============================================================================
#include "RTXDIApplicationBridge.hlsli"
#include "ShaderShared.h"
#include "CommonLighting.hlsli"

// ============================================================================
// SECTION 2B: Additional resources for RTXDIBuildLocalLightPDF_Main and RTXDIBuildEnvLightPDF_Main
// ============================================================================
RWTexture2D<float> g_PDFMip0    : register(u4);  // mip 0 of the local-light PDF texture
RWTexture2D<float> g_EnvPDFMip0 : register(u8);  // mip 0 of the environment-light PDF texture

// Note: g_RTXDI_EnvLightPDFTexture (t19) is declared in RTXDIApplicationBridge.hlsli

#include "Rtxdi/Utils/Checkerboard.hlsli"
#include "Rtxdi/Utils/Math.hlsli"
#include "Rtxdi/DI/Reservoir.hlsli"
#include "Rtxdi/DI/InitialSampling.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"
#include "Rtxdi/DI/BoilingFilter.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"
#include "Rtxdi/LightSampling/PresamplingFunctions.hlsli"

// ============================================================================
// SECTION 2C: NRD includes for RELAX denoising (only when permutation active)
// ============================================================================
#if RTXDI_ENABLE_RELAX_DENOISING
#include "NRDConfig.hlsli"
#include "NRD.hlsli"
#endif

// ============================================================================
// SECTION 3: Helper functions shared across multiple passes
// ============================================================================

RTXDI_LightBufferParameters GetLightBufferParams()
{
    RTXDI_LightBufferParameters p;
    p.localLightBufferRegion.firstLightIndex    = g_RTXDIConst.m_LocalLightFirstIndex;
    p.localLightBufferRegion.numLights          = g_RTXDIConst.m_LocalLightCount;
    p.infiniteLightBufferRegion.firstLightIndex = g_RTXDIConst.m_InfiniteLightFirstIndex;
    p.infiniteLightBufferRegion.numLights       = g_RTXDIConst.m_InfiniteLightCount;
    p.environmentLightParams.lightPresent       = g_RTXDIConst.m_EnvLightPresent;
    p.environmentLightParams.lightIndex         = g_RTXDIConst.m_EnvLightIndex;
    return p;
}

RTXDI_RuntimeParameters GetRuntimeParams()
{
    RTXDI_RuntimeParameters p;
    p.neighborOffsetMask      = g_RTXDIConst.m_NeighborOffsetMask;
    p.activeCheckerboardField = g_RTXDIConst.m_ActiveCheckerboardField;
    return p;
}

RTXDI_ReservoirBufferParameters GetReservoirBufferParams()
{
    RTXDI_ReservoirBufferParameters p;
    p.reservoirBlockRowPitch = g_RTXDIConst.m_ReservoirBlockRowPitch;
    p.reservoirArrayPitch    = g_RTXDIConst.m_ReservoirArrayPitch;
    return p;
}

RTXDI_LightBufferRegion GetLocalLightBufferRegion()
{
    RTXDI_LightBufferRegion r;
    r.firstLightIndex = g_RTXDIConst.m_LocalLightFirstIndex;
    r.numLights       = g_RTXDIConst.m_LocalLightCount;
    r.pad1 = r.pad2   = 0u;
    return r;
}

RTXDI_RISBufferSegmentParameters GetLocalLightRISSegmentParams()
{
    RTXDI_RISBufferSegmentParameters p;
    p.bufferOffset = g_RTXDIConst.m_LocalRISBufferOffset;
    p.tileSize     = g_RTXDIConst.m_LocalRISTileSize;
    p.tileCount    = g_RTXDIConst.m_LocalRISTileCount;
    p.pad1         = 0u;
    return p;
}

RTXDI_RISBufferSegmentParameters GetEnvLightRISBufferSegmentParams()
{
    RTXDI_RISBufferSegmentParameters p;
    p.bufferOffset = g_RTXDIConst.m_EnvRISBufferOffset;
    p.tileSize     = g_RTXDIConst.m_EnvRISTileSize;
    p.tileCount    = g_RTXDIConst.m_EnvRISTileCount;
    p.pad1         = 0u;
    return p;
}

// ============================================================================
// SECTION 4: RTXDIBuildLocalLightPDF
// ============================================================================

[numthreads(8, 8, 1)]
void RTXDI_BuildLocalLightPDF_Main(uint2 gid : SV_DispatchThreadID)
{
    // Bounds check against mip-0 dimensions.
    if (any(gid >= g_RTXDIConst.m_LocalLightPDFTextureSize))
        return;

    // Map texel position → local-light index via Z-curve.
    const uint lightIndex = RTXDI_ZCurveToLinearIndex(gid);

    float weight = 0.0f;
    if (lightIndex < g_RTXDIConst.m_LocalLightCount)
    {
        const uint  globalIndex = lightIndex + g_RTXDIConst.m_LocalLightFirstIndex;
        const GPULight gl       = g_Lights[globalIndex];

        // Type-aware flux (power) computation, matching FullSample PolymorphicLight::getPower.
        // This ensures the PDF texture correctly weights lights by total emitted power,
        // not just a simplified luminance heuristic.
        const float3 radiance = gl.m_Color * gl.m_Intensity;
        const float  lum      = Luminance(radiance);

        if (gl.m_Type == 1u) // Point light: isotropic flux = luminance * 4π
        {
            weight = max(lum * 4.0f * PI, 0.0f);
        }
        else if (gl.m_Type == 2u) // Spot light: integrate smooth cone attenuation over solid angle.
        {
            float cosInner = cos(gl.m_SpotInnerConeAngle);
            float cosOuter = cos(gl.m_SpotOuterConeAngle);
            float coneFactor = (cosInner > cosOuter)
                ? (1.0f - 0.5f * (cosInner + cosOuter))
                : (1.0f - cosOuter);
            weight = max(lum * 2.0f * PI * coneFactor, 0.0f);
        }
        // Type 0 (directional) → weight stays 0: directional lights are infinite lights,
        // handled separately and never presampled via the local light PDF texture.
    }

    g_PDFMip0[gid] = weight;
}

// ============================================================================
// SECTION 4B: RTXDIBuildEnvLightPDF
// Writes luminance(sky) * sin(theta) (or just sin(theta) for BRDF mode)
// into mip-0 of the environment PDF texture so RTXDI_PresampleEnvironmentMap
// can build an importance-sampled env-light RIS buffer.
// ============================================================================

[numthreads(8, 8, 1)]
void RTXDI_BuildEnvLightPDF_Main(uint2 gid : SV_DispatchThreadID)
{
    uint2 pdfSize = g_RTXDIConst.m_EnvPDFTextureSize;
    if (any(gid >= pdfSize))
        return;

    // Map texel to spherical direction (equirectangular lat-long).
    // Matching RTXDI FullSample equirectUVToDirection: 
    //   Azimuth = (uv.x + 0.25) * 2pi
    //   Elevation = (0.5 - uv.y) * pi
    float2 uv    = (float2(gid) + 0.5) / float2(pdfSize);

    float cosElevation;
    float3 dir = equirectUVToDirection(uv, cosElevation);

    // Full luminance * relative solid-angle importance sampling.
    // FullSample's getPixelWeight uses: luma * cos(elevation)
    // Exclude the sun disk (bAddSunDisk=false): the sun is a separate infinite light.
    float3 radiance = GetAtmosphereSkyRadiance(float3(0.0, 0.0, 0.0), dir, g_RTXDIConst.m_SunDirection, g_RTXDIConst.m_SunIntensity, false);
    float lum = Luminance(radiance);
    if (isinf(lum) || isnan(lum))
        lum = 0.0f;
    float weight = max(lum * cosElevation, 0.0f);
    
    g_EnvPDFMip0[gid] = weight;
}

// ============================================================================
// SECTION 4C: RTXDIPresampleEnvironmentMap
// Fills the env-light RIS tile segment by importance-sampling the env PDF
// mip chain (RTXDI_PresampleEnvironmentMap from PresamplingFunctions.hlsli).
// Dispatch: (DivUp(k_EnvRISTileSize,256), k_EnvRISTileCount, 1)
// ============================================================================

[numthreads(RTXDI_PRESAMPLING_GROUP_SIZE, 1, 1)]
void RTXDI_PresampleEnvironmentMap_Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    const uint sampleInTile = GlobalIndex.x;
    const uint tileIndex    = GlobalIndex.y;

    if (sampleInTile >= g_RTXDIConst.m_EnvRISTileSize ||
        tileIndex    >= g_RTXDIConst.m_EnvRISTileCount)
        return;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(uint2(sampleInTile, tileIndex), g_RTXDIConst.m_FrameIndex, 0);

    RTXDI_PresampleEnvironmentMap(
        rng,
        g_RTXDI_EnvLightPDFTexture,
        g_RTXDIConst.m_EnvPDFTextureSize,
        tileIndex,
        sampleInTile,
        GetEnvLightRISBufferSegmentParams());
}

// ============================================================================
// SECTION 5: RTXDIGenerateInitialSamples
// ============================================================================

[numthreads(8, 8, 1)]
void RTXDI_GenerateInitialSamples_Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    RTXDI_RuntimeParameters rtParams = GetRuntimeParams();
    RTXDI_ReservoirBufferParameters rbp = GetReservoirBufferParams();

    uint2 reservoirPosition = GlobalIndex;
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(reservoirPosition, rtParams.activeCheckerboardField);
    uint2 viewportSize = g_RTXDIConst.m_ViewportSize;
    if (any(pixelPosition >= viewportSize))
        return;

    int2  iPixel = int2(pixelPosition);

    // Initialise RNG — use SDK's RTXDI_InitRandomSampler with the named per-pass seed constants.
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_RTXDIConst.m_FrameIndex, RTXDI_DI_GENERATE_INITIAL_SAMPLES_RANDOM_SEED);
    RTXDI_RandomSamplerState tileRng = RTXDI_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, g_RTXDIConst.m_FrameIndex, RTXDI_DI_GENERATE_INITIAL_SAMPLES_RANDOM_SEED);

    // Fetch G-buffer surface for this pixel
    RAB_Surface surface = RAB_GetGBufferSurface(iPixel, false);

    RTXDI_LightBufferParameters lbp = GetLightBufferParams();

    // Use sample counts from the constant buffer — these are set by RTXDIRenderer.cpp
    // from the UI-controlled g_ReSTIRDI_InitialSamplingParams.
    uint numLocalSamples = (lbp.localLightBufferRegion.numLights > 0u)
        ? g_RTXDIConst.m_NumLocalLightSamples : 0u;
    uint numInfiniteSamples = (lbp.infiniteLightBufferRegion.numLights > 0u)
        ? g_RTXDIConst.m_NumInfiniteLightSamples : 0u;
    uint numEnvSamples = (g_RTXDIConst.m_EnvLightPresent != 0u)
        ? g_RTXDIConst.m_NumEnvSamples : 0u;
    uint numBrdfSamples = g_RTXDIConst.m_NumBrdfSamples;

    ReSTIRDI_LocalLightSamplingMode localLightSamplingMode =
        (ReSTIRDI_LocalLightSamplingMode)g_RTXDIConst.m_LocalLightSamplingMode;

#if RTXDI_REGIR_MODE == RTXDI_REGIR_DISABLED
    // ReGIR mode silently falls back to uniform sampling when ReGIR is not compiled.
    // Mirror FullSample behavior by remapping it to Power_RIS in this configuration.
    if (localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode_REGIR_RIS)
    {
        localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode_POWER_RIS;
    }
#endif

    RTXDI_DIInitialSamplingParameters sampleParams;
    sampleParams.numLocalLightSamples       = numLocalSamples;
    sampleParams.numInfiniteLightSamples    = numInfiniteSamples;
    sampleParams.numEnvironmentSamples      = numEnvSamples;
    sampleParams.numBrdfSamples             = numBrdfSamples;
    sampleParams.brdfCutoff                 = g_RTXDIConst.m_BrdfCutoff;
    sampleParams.localLightSamplingMode     = localLightSamplingMode;
    sampleParams.enableInitialVisibility    = false; // handled manually below
    sampleParams.environmentMapImportanceSampling = (g_RTXDIConst.m_EnvSamplingMode == 2u);

    // Build RIS segment parameters from the constant buffer.
    RTXDI_RISBufferSegmentParameters localRISParams = GetLocalLightRISSegmentParams();
    RTXDI_RISBufferSegmentParameters envRISParams   = GetEnvLightRISBufferSegmentParams();

    RAB_LightSample selectedSample;
    RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(
        rng, tileRng, surface, sampleParams, lbp,
        #if RTXDI_ENABLE_PRESAMPLING
            localRISParams,
            envRISParams,
        #endif
        selectedSample);

    // Initial visibility: trace a conservative shadow ray for the selected sample.
    // Note: FullSample 3.0.0 does NOT perform initial visibility here — it relies on
    // the temporal/spatial passes. We keep it as an optional quality improvement,
    // gated on m_EnableInitialVisibility.
    if (g_RTXDIConst.m_EnableInitialVisibility != 0u && RTXDI_IsValidDIReservoir(reservoir))
    {
        if (!RAB_GetConservativeVisibility(surface, selectedSample))
        {
            RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
        }
    }

    RTXDI_StoreDIReservoir(reservoir, rbp, reservoirPosition,
        g_RTXDIConst.m_InitialSamplingOutputBufferIndex);
}

// ============================================================================
// SECTION 6: RTXDIPresampleLights
// ============================================================================

[numthreads(RTXDI_PRESAMPLING_GROUP_SIZE, 1, 1)]
void RTXDI_PresampleLights_Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    const uint sampleInTile = GlobalIndex.x;
    const uint tileIndex    = GlobalIndex.y;

    if (sampleInTile >= g_RTXDIConst.m_LocalRISTileSize ||
        tileIndex    >= g_RTXDIConst.m_LocalRISTileCount)
        return;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(uint2(sampleInTile, tileIndex), g_RTXDIConst.m_FrameIndex, 0);

    // Delegate entirely to the RTXDI SDK function.    //   - descend the PDF mip chain to pick a light proportional to its weight
    //   - call RAB_StoreCompactLightInfo to cache the light data
    //   - write the (index, invPdf) pair to RTXDI_RIS_BUFFER
    RTXDI_PresampleLocalLights(
        rng,
        g_RTXDI_LocalLightPDFTexture,               // Texture2D<float> with full mip chain
        g_RTXDIConst.m_LocalLightPDFTextureSize,    // mip-0 dimensions
        tileIndex,
        sampleInTile,
        GetLocalLightBufferRegion(),
        GetLocalLightRISSegmentParams());
}

// ============================================================================
// SECTION 7: RTXDITemporalResampling
// ============================================================================

[numthreads(8, 8, 1)]
void RTXDI_TemporalResampling_Main(
    uint2 GlobalIndex : SV_DispatchThreadID,
    uint2 LocalIndex  : SV_GroupThreadID)
{
    RTXDI_RuntimeParameters        rtParams = GetRuntimeParams();
    RTXDI_ReservoirBufferParameters rbp      = GetReservoirBufferParams();

    uint2 reservoirPosition = GlobalIndex;
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(reservoirPosition, rtParams.activeCheckerboardField);
    uint2 viewportSize = g_RTXDIConst.m_ViewportSize;
    if (any(pixelPosition >= viewportSize))
        return;

    int2  iPixel        = int2(pixelPosition);

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_RTXDIConst.m_FrameIndex, RTXDI_DI_TEMPORAL_RESAMPLING_RANDOM_SEED);

    // Load current surface
    RAB_Surface surface = RAB_GetGBufferSurface(iPixel, false);

    RTXDI_DIReservoir outReservoir = RTXDI_EmptyDIReservoir();
    if (RAB_IsSurfaceValid(surface))
    {
        // Load the reservoir produced by the initial sampling pass (current frame's new candidates)
        RTXDI_DIReservoir curReservoir = RTXDI_LoadDIReservoir(rbp, reservoirPosition,
            g_RTXDIConst.m_InitialSamplingOutputBufferIndex);

        // Motion vector — stored as pixel-space velocity (dx, dy).
        float3 mv = g_GBufferMV.Load(int3(iPixel, 0)).xyz;
        float3 pixelMotion = ConvertMotionVectorToPixelSpace(g_RTXDIConst.m_View, g_RTXDIConst.m_PrevView, iPixel, mv);

        // Permutation sampling: enabled by the constant buffer flag, but disabled
        // for complex (low-roughness) surfaces to avoid noise on mirror-like materials.
        bool usePermutationSampling = (g_RTXDIConst.m_TemporalEnablePermutationSampling != 0u)
            && !IsComplexSurface(iPixel, surface);

        RTXDI_DITemporalResamplingParameters tparams;
        tparams.maxHistoryLength          = g_RTXDIConst.m_TemporalMaxHistoryLength;
        tparams.biasCorrectionMode        = g_RTXDIConst.m_TemporalBiasCorrectionMode;
        tparams.depthThreshold            = g_RTXDIConst.m_TemporalDepthThreshold;
        tparams.normalThreshold           = g_RTXDIConst.m_TemporalNormalThreshold;
        tparams.enableVisibilityShortcut  = g_RTXDIConst.m_TemporalEnableVisibilityShortcut != 0u;
        tparams.enablePermutationSampling = usePermutationSampling;
        tparams.uniformRandomNumber       = g_RTXDIConst.m_TemporalUniformRandomNumber;

        RAB_LightSample selectedSample = RAB_EmptyLightSample();
        int2 temporalPixelPos;

        outReservoir = RTXDI_DITemporalResampling(
            pixelPosition, surface, curReservoir, rng,
            rtParams, rbp,
            pixelMotion,
            g_RTXDIConst.m_TemporalResamplingInputBufferIndex,
            tparams,
            temporalPixelPos, selectedSample);
    }

    // Boiling filter (operates within the 8×8 group)
    if (g_RTXDIConst.m_TemporalEnableBoilingFilter != 0u)
    {
        RTXDI_BoilingFilter(LocalIndex, g_RTXDIConst.m_TemporalBoilingFilterStrength, outReservoir);
    }

    RTXDI_StoreDIReservoir(outReservoir, rbp, reservoirPosition,
        g_RTXDIConst.m_TemporalResamplingOutputBufferIndex);
}

// ============================================================================
// SECTION 8: RTXDISpatialResampling
// ============================================================================

[numthreads(8, 8, 1)]
void RTXDI_SpatialResampling_Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    RTXDI_RuntimeParameters         rtParams = GetRuntimeParams();
    RTXDI_ReservoirBufferParameters rbp      = GetReservoirBufferParams();

    uint2 reservoirPosition = GlobalIndex;
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(reservoirPosition, rtParams.activeCheckerboardField);
    uint2 viewportSize = g_RTXDIConst.m_ViewportSize;
    if (any(pixelPosition >= viewportSize))
        return;

    int2  iPixel        = int2(pixelPosition);

    // Use pass index 3 (distinct from initial=1, temporal=2) for decorrelated RNG
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_RTXDIConst.m_FrameIndex, RTXDI_DI_SPATIAL_RESAMPLING_RANDOM_SEED);

    RAB_Surface surface = RAB_GetGBufferSurface(iPixel, false);

    // Default to an empty reservoir in case the pixel is invalid
    RTXDI_DIReservoir outReservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(surface))
    {
        // Load the centre pixel's reservoir (temporal output, or initial output if temporal is off)
        RTXDI_DIReservoir centerReservoir = RTXDI_LoadDIReservoir(rbp, reservoirPosition,
            g_RTXDIConst.m_SpatialResamplingInputBufferIndex);

        RTXDI_DISpatialResamplingParameters sparams;
        sparams.numSamples                    = g_RTXDIConst.m_SpatialNumSamples;
        sparams.numDisocclusionBoostSamples   = g_RTXDIConst.m_SpatialNumDisocclusionBoostSamples;
        sparams.targetHistoryLength           = g_RTXDIConst.m_TemporalMaxHistoryLength; // match temporal history length
        sparams.biasCorrectionMode            = g_RTXDIConst.m_SpatialBiasCorrectionMode;
        sparams.samplingRadius                = g_RTXDIConst.m_SpatialSamplingRadius;
        sparams.depthThreshold                = g_RTXDIConst.m_SpatialDepthThreshold;
        sparams.normalThreshold               = g_RTXDIConst.m_SpatialNormalThreshold;
        sparams.enableMaterialSimilarityTest  = true;
        sparams.discountNaiveSamples          = g_RTXDIConst.m_SpatialDiscountNaiveSamples != 0u;

        RAB_LightSample selectedSample = RAB_EmptyLightSample();
        outReservoir = RTXDI_DISpatialResampling(
            pixelPosition, surface, centerReservoir,
            rng, rtParams, rbp,
            g_RTXDIConst.m_SpatialResamplingInputBufferIndex,
            sparams, selectedSample);
    }

    RTXDI_StoreDIReservoir(outReservoir, rbp, reservoirPosition,
        g_RTXDIConst.m_SpatialResamplingOutputBufferIndex);
}

// ============================================================================
// SECTION 9: RTXDIShadeSamples
// ============================================================================

[numthreads(8, 8, 1)]
void RTXDI_ShadeSamples_Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    RTXDI_ReservoirBufferParameters rbp = GetReservoirBufferParams();
    RTXDI_RuntimeParameters rtParams = GetRuntimeParams();

    uint2 reservoirPosition = GlobalIndex;
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(reservoirPosition, rtParams.activeCheckerboardField);
    uint2 viewportSize = g_RTXDIConst.m_ViewportSize;
    if (any(pixelPosition >= viewportSize))
        return;

    int2 iPixel = int2(pixelPosition);

    RAB_Surface surface = RAB_GetGBufferSurface(iPixel, false);
    bool surfaceValid = RAB_IsSurfaceValid(surface);

    // Load the final shading reservoir (temporal or initial output, depending on which passes ran)
    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(rbp, reservoirPosition, g_RTXDIConst.m_ShadingInputBufferIndex);

    float3 diffuseDemodulated = float3(0.0, 0.0, 0.0);
    float3 specularDemodulated = float3(0.0, 0.0, 0.0);
    float hitDistance = 0.0;
    bool needToStore = false;

    if (surfaceValid && RTXDI_IsValidDIReservoir(reservoir))
    {
        // Reconstruct the selected light sample from the reservoir.
        uint lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
        float2 randXY = RTXDI_GetDIReservoirSampleUV(reservoir);

        RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, randXY);

        if (lightSample.solidAnglePdf > 0.0)
        {
            if (g_RTXDIConst.m_EnableFinalVisibility != 0u)
            {
                float3 visibility = 0;
                bool visibilityReused = false;

                if (g_RTXDIConst.m_ReuseFinalVisibility != 0u)
                {
                    RTXDI_VisibilityReuseParameters rparams;
                    rparams.maxAge      = g_RTXDIConst.m_FinalVisibilityMaxAge;
                    rparams.maxDistance = g_RTXDIConst.m_FinalVisibilityMaxDistance;
                    visibilityReused = RTXDI_GetDIReservoirVisibility(reservoir, rparams, visibility);
                }

                if (!visibilityReused)
                {
                    bool visible = GetFinalVisibility(g_SceneAS, surface, lightSample.position);
                    visibility = visible ? 1.0 : 0.0;
                    RTXDI_StoreVisibilityInDIReservoir(reservoir, visibility,
                        g_RTXDIConst.m_DiscardInvisibleSamples != 0u);
                    needToStore = true;
                }

                lightSample.radiance *= visibility;
            }

            lightSample.radiance *= RTXDI_GetDIReservoirInvPdf(reservoir) / lightSample.solidAnglePdf;

            if (any(lightSample.radiance > 0.0))
            {
                float3 V = RAB_GetSurfaceViewDirection(surface);
                float3 L = lightSample.direction;
                float3 H = normalize(V + L);

                float NdotL = saturate(dot(surface.normal, L));
                float NdotV = saturate(dot(surface.normal, V));
                float LdotH = saturate(dot(L, H));

                // Disney Burley diffuse (returns Fd * NdotL / PI)
                float diffuseTerm = DisneyBurleyDiffuse(NdotL, NdotV, LdotH, surface.roughness);
                diffuseDemodulated = lightSample.radiance * diffuseTerm;

                static const float kMinRoughness = 0.05;
                float3 specular = GGXTimesNdotL_Exact(
                    V,
                    L,
                    surface.normal,
                    max(surface.material.roughness, kMinRoughness),
                    surface.material.specularF0);

                specularDemodulated = lightSample.radiance * specular / max(surface.material.specularF0, float3(0.01, 0.01, 0.01));

                hitDistance = length(lightSample.position - surface.worldPos);
            }
        }
    }

    // Write back the reservoir if visibility was freshly traced and stored.
    if (needToStore)
    {
        RTXDI_StoreDIReservoir(reservoir, rbp, reservoirPosition,
            g_RTXDIConst.m_ShadingInputBufferIndex);
    }

#if RTXDI_ENABLE_RELAX_DENOISING
    // In denoiser mode, shading output is in reservoir space to match checkerboard behavior.
    g_RTXDIDiffuseOutput[reservoirPosition]  = RELAX_FrontEnd_PackRadianceAndHitDist(diffuseDemodulated, hitDistance, true);
    g_RTXDISpecularOutput[reservoirPosition] = RELAX_FrontEnd_PackRadianceAndHitDist(specularDemodulated, hitDistance, true);
#else
    g_RTXDIDIOutput[pixelPosition] = float4(diffuseDemodulated, 1.0);
    g_RTXDISpecularOutput[pixelPosition] = float4(specularDemodulated, 1.0);

    // we never enable checkerboard copying in RELAX mode, so no need for the extra complexity of writing to the other field's pixel
#if 0
    if (rtParams.activeCheckerboardField != 0u)
    {
        int2 otherFieldPixel = int2(pixelPosition);
        otherFieldPixel.x += ((rtParams.activeCheckerboardField == 1u) == ((pixelPosition.y & 1u) != 0u)) ? 1 : -1;

        if (all(otherFieldPixel >= int2(0, 0)) && all(otherFieldPixel < int2(viewportSize)))
        {
            g_RTXDIDIOutput[otherFieldPixel] = float4(diffuseDemodulated, 1.0);
            g_RTXDISpecularOutput[otherFieldPixel] = float4(specularDemodulated, 1.0);
        }
    }
#endif
#endif
}

// ============================================================================
// Linear view-space depth (IN_VIEWZ) required by the NRD RELAX denoiser.
// Runs full-screen — sky pixels get a large sentinel so NRD skips them.
// Only compiled when RTXDI_ENABLE_RELAX_DENOISING=1.
// ============================================================================
#if RTXDI_ENABLE_RELAX_DENOISING
[numthreads(8, 8, 1)]
void RTXDI_GenerateViewZ_Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    uint2 viewportSize = g_RTXDIConst.m_ViewportSize;
    if (any(GlobalIndex >= viewportSize))
        return;

    float depth = g_Depth.Load(int3(GlobalIndex, 0));

    float linearDepth;
    if (depth == 0.0) // Reverse-Z: 0 == far plane (sky / background)
    {
        // Write a large sentinel so NRD skips sky pixels.
        linearDepth = 1e6f;
    }
    else
    {
        linearDepth = ConvertToLinearDepth(depth, g_RTXDIConst.m_View.m_MatViewToClip);
    }

    g_RTXDILinearDepth[GlobalIndex] = linearDepth;
}
#endif