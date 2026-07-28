// ScreenSpaceShadows.hlsl — Screen-space shadow compute shader
// Uses radial wavefront dispatch to ray-march the depth buffer along the light direction.
// Min-blends its result into the existing CSM shadow mask.

#include "srrhi/hlsl/Common.hlsli"
#include "srrhi/hlsl/ScreenSpaceShadows.hlsli"

// Configuration macros required by the GPU implementation
#define WAVE_SIZE 64
#define SAMPLE_COUNT SS_SHADOW_SAMPLE_COUNT
#define HARD_SHADOW_SAMPLES 4
#define FADE_OUT_SAMPLES 8

// Globals from srrhi bindings
static const srrhi::ScreenSpaceShadowConstants g_CB           = srrhi::ScreenSpaceShadowInputs::GetCB();
static const Texture2D<float>                  g_DepthTexture = srrhi::ScreenSpaceShadowInputs::GetDepthTexture();
static       RWTexture2D<float>                g_OutputTex    = srrhi::ScreenSpaceShadowInputs::GetOutputTexture();
static const SamplerState                      g_PointBorder  = srrhi::ScreenSpaceShadowInputs::GetPointBorderSampler();

// Include the GPU implementation (defines DispatchParameters struct and WriteScreenSpaceShadow)
#include "../../external/bend_sss_gpu.h"

// Build the DispatchParameters struct from our constant buffer.
// Must be defined AFTER the include since DispatchParameters is declared there.
static DispatchParameters BuildParams()
{
    DispatchParameters p;
    p.SetDefaults();

    p.SurfaceThickness           = g_CB.m_SurfaceThickness;
    p.BilinearThreshold          = g_CB.m_BilinearThreshold;
    p.ShadowContrast             = g_CB.m_ShadowContrast;
    p.IgnoreEdgePixels           = (g_CB.m_Flags & 0x1) != 0;
    p.UsePrecisionOffset         = (g_CB.m_Flags & 0x2) != 0;
    p.BilinearSamplingOffsetMode = (g_CB.m_Flags & 0x4) != 0;
    p.UseEarlyOut                = (g_CB.m_Flags & 0x8) != 0;
    p.DebugOutputEdgeMask        = false;
    p.DebugOutputThreadIndex     = false;
    p.DebugOutputWaveIndex       = false;
    p.DepthBounds                = g_CB.m_DepthBounds;
    p.LightCoordinate            = g_CB.m_LightCoordinate;
    p.WaveOffset                 = g_CB.m_WaveOffset;
    p.FarDepthValue              = g_CB.m_FarDepthValue;
    p.NearDepthValue             = g_CB.m_NearDepthValue;
    p.InvDepthTextureSize        = g_CB.m_InvDepthTextureSize;
    p.DepthTexture               = g_DepthTexture;
    p.OutputTexture              = g_OutputTex;
    p.PointBorderSampler         = g_PointBorder;

    return p;
}

[numthreads(WAVE_SIZE, 1, 1)]
void ScreenSpaceShadows_CSMain(uint3 groupID : SV_GroupID, uint groupThreadID : SV_GroupThreadID)
{
    DispatchParameters params = BuildParams();
    WriteScreenSpaceShadow(params, (int3)groupID, (int)groupThreadID);
}
