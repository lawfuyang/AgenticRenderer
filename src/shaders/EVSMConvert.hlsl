// EVSMConvert.hlsl — converts D32_FLOAT CSM depth to RGBA16_FLOAT EVSM moments (mip 0)
#include "srrhi/hlsl/EVSMConvert.hlsli"

static const srrhi::EVSMConvertConstants g_CB    = srrhi::EVSMConvertInputs::GetCB();
static const Texture2DArray<float>       g_Depth = srrhi::EVSMConvertInputs::GetDepthMap();
static       RWTexture2DArray<float4>    g_EVSM  = srrhi::EVSMConvertInputs::GetEVSMMap();

[numthreads(8, 8, 1)]
void EVSMConvert_CSMain(uint3 id : SV_DispatchThreadID)
{
    uint2 xy = id.xy;
    uint  W, H, slices;
    g_Depth.GetDimensions(W, H, slices);
    if (any(xy >= uint2(W, H))) return;

    float d  = g_Depth.Load(int4(xy, g_CB.m_CascadeIndex, 0));
    // Convert reversed-Z [1=near, 0=far] to linear [0=near, 1=far]
    float linearDepth = 1.0f - d;
    // Remap to [-1, 1] range: near(0)->-1, far(1)->+1
    float depth = linearDepth * 2.0f - 1.0f;
    float c  = g_CB.m_VsmExponent;
    // Positive warp
    float pw = exp(c * depth);
    // Negative warp (stored as negative)
    float nw = -1.0f / pw;
    g_EVSM[uint3(xy, g_CB.m_CascadeIndex)] = float4(pw, pw * pw, nw, nw * nw);
}
