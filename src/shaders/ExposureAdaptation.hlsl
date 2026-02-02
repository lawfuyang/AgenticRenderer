#include "ShaderShared.h"

RWStructuredBuffer<float> Exposure : register(u0);
StructuredBuffer<uint> HistogramInput : register(t0);

PUSH_CONSTANT ConstantBuffer<AdaptationConstants> AdaptationCB : register(b0);

groupshared float SharedWeights[256];

[numthreads(256, 1, 1)]
void ExposureAdaptation_CSMain(uint threadIdx : SV_GroupIndex)
{
    uint count = HistogramInput[threadIdx];
    
    float range = AdaptationCB.m_MaxLogLuminance - AdaptationCB.m_MinLogLuminance;
    float logLum = threadIdx == 0 ? AdaptationCB.m_MinLogLuminance : (AdaptationCB.m_MinLogLuminance + (float(threadIdx - 1) / 254.0f) * range);
    SharedWeights[threadIdx] = float(count) * logLum;

    GroupMemoryBarrierWithGroupSync();

    for (uint i = 128; i > 0; i >>= 1)
    {
        if (threadIdx < i)
            SharedWeights[threadIdx] += SharedWeights[threadIdx + i];
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIdx == 0)
    {
        float avgLogLum = SharedWeights[0] / max(float(AdaptationCB.m_NumPixels), 1.0f);
        float avgLum = exp2(avgLogLum);
        
        float targetExposure = AdaptationCB.m_KeyValue / max(avgLum, 0.0001f);
        
        float currentExposure = Exposure[0];
        float exposure = currentExposure + (targetExposure - currentExposure) * (1.0f - exp(-AdaptationCB.m_DeltaTime * AdaptationCB.m_AdaptationSpeed));
        
        Exposure[0] = exposure;
    }
}
