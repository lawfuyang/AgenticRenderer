#pragma once

#include "ShaderShared.h"

bool FrustumSphereTest(
    float3 centerVS,
    float radius,
    float4 planes[5]   // view-space frustum planes
)
{
    // Test against each frustum plane
    [unroll]
    for (int i = 0; i < 5; i++)
    {
        float3 n = planes[i].xyz;
        float d  = planes[i].w;

        // Signed distance from sphere center to plane
        float dist = dot(n, centerVS) + d;

        // If sphere is completely outside this plane
        if (dist < -radius)
            return false;
    }

    return true;
}
