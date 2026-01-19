#include "ShaderShared.h"

cbuffer DownsampleCB : register(b0)
{
    DownsampleConstants g_DownsampleConstants;
};

SamplerState g_Sampler : register(s0);
RWTexture2DArray<float> g_HZB : register(u0);

[numthreads(8,8,1)]
void Downsample_CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 coord = dispatchThreadId.xy;
    if (coord.x >= g_DownsampleConstants.m_Width || coord.y >= g_DownsampleConstants.m_Height) return;
    
    // Read 4 texels from input mip and perform max operation
    uint inputSlice = g_DownsampleConstants.m_InputMip;
    float d0 = g_HZB.Load(uint3(2 * coord.x, 2 * coord.y, inputSlice));
    float d1 = g_HZB.Load(uint3(2 * coord.x + 1, 2 * coord.y, inputSlice));
    float d2 = g_HZB.Load(uint3(2 * coord.x, 2 * coord.y + 1, inputSlice));
    float d3 = g_HZB.Load(uint3(2 * coord.x + 1, 2 * coord.y + 1, inputSlice));
    float maxValue = max(max(d0, d1), max(d2, d3));
    
    // Output to output mip
    g_HZB[uint3(coord, g_DownsampleConstants.m_OutputMip)] = maxValue;
}