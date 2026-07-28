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
    float nd = d * 2.0f - 1.0f;   // reversed-Z [0,1] -> [-1,1]: near(1)->+1, far(0)->-1
    float c  = g_CB.m_VsmExponent;
    float pw = exp( c * nd);
    float nw = exp(-c * nd);
    g_EVSM[uint3(xy, g_CB.m_CascadeIndex)] = float4(pw, pw * pw, nw, nw * nw);
}
