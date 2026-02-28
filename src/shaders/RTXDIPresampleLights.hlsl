/*
 * RTXDIPresampleLights.hlsl
 *
 * Pre-sampling pass: runs once per frame BEFORE the initial sampling pass.
 * Populates the RIS tile buffer with power-importance-sampled local lights
 * using the RTXDI SDK's RTXDI_PresampleLocalLights function.
 *
 * RTXDI_PresampleLocalLights does hierarchical importance sampling (mipmap
 * descent) through the local-light PDF texture built by RTXDIBuildLocalLightPDF.
 * Lights with higher luminance (power) are selected proportionally more often,
 * which is the actual definition of Power-RIS presampling.
 *
 * For each RIS tile entry it:
 *   1. Descends the PDF mip chain to pick a light proportional to its weight.
 *   2. Calls RAB_StoreCompactLightInfo to cache the light in g_RTXDI_RISLightDataBuffer.
 *   3. Writes the (lightIndex | COMPACT_BIT, invSourcePdf) pair to g_RTXDI_RISBuffer.
 *
 * The initial sampling pass then reads these tiles to select candidate lights
 * during RTXDI_SampleLightsForSurface (Power_RIS mode).
 *
 * Must run every frame — light radiance can change between frames, which would
 * invalidate the PDF texture built just before this pass.
 *
 * Dispatch: (ceil(tileSize / RTXDI_PRESAMPLING_GROUP_SIZE), tileCount, 1)
 *   GlobalIndex.x = sampleInTile  [0 .. tileSize-1]
 *   GlobalIndex.y = tileIndex     [0 .. tileCount-1]
 */

#include "RTXDIApplicationBridge.hlsli"

// SDK presampling function (requires RTXDI_RIS_BUFFER and RTXDI_ENABLE_PRESAMPLING=1)
#include "Rtxdi/LightSampling/PresamplingFunctions.hlsli"

// ============================================================================
#define RTXDI_PRESAMPLING_GROUP_SIZE 256

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

// ============================================================================
[numthreads(RTXDI_PRESAMPLING_GROUP_SIZE, 1, 1)]
void CSMain(uint2 GlobalIndex : SV_DispatchThreadID)
{
    const uint sampleInTile = GlobalIndex.x;
    const uint tileIndex    = GlobalIndex.y;

    if (sampleInTile >= g_RTXDIConst.m_LocalRISTileSize ||
        tileIndex    >= g_RTXDIConst.m_LocalRISTileCount)
        return;

    if (g_RTXDIConst.m_LocalLightCount == 0u)
    {
        const uint risBufferPtr = g_RTXDIConst.m_LocalRISBufferOffset
                                + tileIndex * g_RTXDIConst.m_LocalRISTileSize
                                + sampleInTile;
        g_RTXDI_RISBuffer[risBufferPtr] = uint2(RTXDI_INVALID_LIGHT_INDEX, 0u);
        return;
    }

    // Per-entry RNG.  Using a 2D seed so (tile, sample) pairs don't collide.
    RAB_RandomSamplerState rng = RAB_InitRandomSampler(
        uint2(sampleInTile + tileIndex * g_RTXDIConst.m_LocalRISTileSize,
              g_RTXDIConst.m_FrameIndex), 0u);

    // Delegate entirely to the RTXDI SDK function.  It will:
    //   - descend the PDF mip chain to pick a light proportional to its weight
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