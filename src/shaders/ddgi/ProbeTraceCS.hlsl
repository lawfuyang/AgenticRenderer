// DDGI Probe Trace Compute Shader (stub)
// Dispatched per-volume: each thread traces a single probe ray.


[numthreads(64, 1, 1)]
void ProbeTraceCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    // Stub: full implementation will:
    //   1. Compute probe index / ray index from DispatchThreadID
    //   2. Load DDGIVolumeDescGPU constants
    //   3. Compute probe world position via DDGIGetProbeWorldPosition()
    //   4. Compute ray direction via DDGIGetProbeRayDirection()
    //   5. Trace inline ray using RayQuery against TLAS
    //   6. Evaluate direct lighting at hit point, sample irradiance at miss
    //   7. Write radiance + distance to probe RayData texture array
}
