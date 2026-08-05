// DDGI Debug Compositor — fullscreen compute shader.
// Reads the DDGI debug overlay, additive-blends it onto the current UAV value
// (which is a copy of the post-bloom HDR colour from the render graph).
// No separate HDR-colour SRV needed — UAV reads provide the old value.

#include "Common.hlsli"

Texture2D<float4>   g_DDGIDebugOverlay : register(t0);
RWTexture2D<float4> g_Output           : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 px = dispatchThreadID.xy;

    float4 overlay = g_DDGIDebugOverlay.Load(int3(px, 0));

    // Debug overlay replaces the HDR pixel where it has content.
    // (Pure-black probes like zero-irradiance must still show as spheres.)
    if (overlay.a > 0.0f)
    {
        g_Output[px] = float4(overlay.rgb, 1.0f);
    }
    // else: leave the HDR pixel as-is
}
