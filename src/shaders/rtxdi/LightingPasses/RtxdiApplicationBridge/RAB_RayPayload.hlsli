/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef RAB_RAY_PAYLOAD_HLSLI
#define RAB_RAY_PAYLOAD_HLSLI

#include "../../RaytracingCommon.hlsli"

struct RAB_RayPayload
{
    float3 throughput;
    float committedRayT;
    uint instanceID;
    uint geometryIndex;
    uint primitiveIndex;
    bool frontFace;
    float2 barycentrics;
};

bool RAB_IsValidHit(RAB_RayPayload rayPayload)
{
    return (rayPayload.committedRayT > 0) &&
           (rayPayload.instanceID != ~0u);
}

float RAB_RayPayloadGetCommittedHitT(RAB_RayPayload rayPayload)
{
    return rayPayload.committedRayT;
}

bool RAB_RayPayloadIsFrontFace(RAB_RayPayload rayPayload)
{
	return rayPayload.frontFace;
}

bool RAB_RayPayloadIsTwoSided(RAB_RayPayload rayPayload)
{
	return false;
}

float3 RAB_RayPayloadGetEmittedRadiance(RAB_RayPayload rayPayload)
{
    srrhi::PerInstanceData instance = t_InstanceData[rayPayload.instanceID];
    srrhi::MaterialConstants mat    = t_MaterialConstants[instance.m_MaterialIndex];

    float3 emissive = mat.m_EmissiveFactor.rgb;

    // Sample emissive texture if present (matching FullSample's sampleGeometryMaterial behavior)
    if ((mat.m_TextureFlags & srrhi::CommonConsts::TEXFLAG_EMISSIVE) != 0)
    {
        srrhi::MeshData geometry = t_GeometryData[instance.m_MeshDataIndex];
        float2 uv = GetInterpolatedUV(
            rayPayload.primitiveIndex,
            instance.m_LODIndex,
            rayPayload.barycentrics,
            geometry,
            t_SceneIndices,
            t_SceneVertices);

        emissive *= SampleBindlessTextureLevel(mat.m_EmissiveTextureIndex, mat.m_EmissiveSamplerIndex, uv, 0).rgb;
    }

    return emissive;
}

#endif // RAB_RAY_PAYLOAD_HLSLI