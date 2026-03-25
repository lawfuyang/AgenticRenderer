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

#ifndef RAB_BUFFER_HLSLI
#define RAB_BUFFER_HLSLI

#include <SharedShaderInclude/ShaderParameters.h>

// ---- Constant buffers ----
ConstantBuffer<ResamplingConstants> g_Const : register(b0);

// ---- SRVs (match RTXDIRenderer.cpp binding layout) ----
// t0  = t_NeighborOffsets
// t1  = t_GBufferDepth
// t2  = t_GBufferGeoNormals     (RG16_FLOAT: oct-encoded geometric normal)
// t3  = t_GBufferAlbedo         (RGBA8_UNORM: baseColor.rgb + alpha)
// t4  = t_GBufferORM            (RG8_UNORM: roughness=.r, metallic=.g)
// t5  = t_GBufferNormals        (RG16_FLOAT: encoded, decoded via DecodeNormal)
// t6  = t_PrevGBufferNormals    (= m_GbufferNormalsHistory)
// t7  = t_PrevGBufferGeoNormals (= m_GeoNormalsHistory)
// t8  = t_PrevGBufferAlbedo     (= m_GBufferAlbedoHistory)
// t9  = t_PrevGBufferORM        (= m_GBufferORMHistory)
// t10 = t_MotionVectors
// t11 = t_DenoiserNormalRoughness
// t12 = t_PrevGBufferDepth
// t13 = t_LocalLightPdfTexture
// t14 = t_EnvironmentPdfTexture
// t15 = SceneBVH
// t16 = PrevSceneBVH
// t17 = t_LightDataBuffer
// t18 = t_GBufferEmissive
// t19 = t_GeometryInstanceToLight
// t20 = t_InstanceData  (PerInstanceData)
// t21 = t_GeometryData  (MeshData)
// t22 = t_MaterialConstants
// t23 = t_SceneIndices
// t24 = t_SceneVertices

Buffer<float2>                              t_NeighborOffsets           : register(t0);
Texture2D<float>                            t_GBufferDepth              : register(t1);
Texture2D<float2>                           t_GBufferGeoNormals         : register(t2);  // RG16_FLOAT: oct-encoded geometric normal
Texture2D<float4>                           t_GBufferAlbedo             : register(t3);  // RGBA8_UNORM: baseColor.rgb + alpha
Texture2D<float2>                           t_GBufferORM                : register(t4);  // RG8_UNORM: roughness=.r, metallic=.g
Texture2D<float2>                           t_GBufferNormals            : register(t5);  // RG16_FLOAT: encoded, use DecodeNormal
Texture2D<float2>                           t_PrevGBufferNormals        : register(t6);  // = m_GbufferNormalsHistory
Texture2D<float2>                           t_PrevGBufferGeoNormals     : register(t7);  // = m_GeoNormalsHistory
Texture2D<float4>                           t_PrevGBufferAlbedo         : register(t8);  // = m_GBufferAlbedoHistory
Texture2D<float2>                           t_PrevGBufferORM            : register(t9);  // = m_GBufferORMHistory
Texture2D<float4>                           t_MotionVectors             : register(t10);
Texture2D<float3>                           t_DenoiserNormalRoughness   : register(t11);
Texture2D<float>                            t_PrevGBufferDepth          : register(t12);
Texture2D                                   t_LocalLightPdfTexture      : register(t13);
Texture2D                                   t_EnvironmentPdfTexture     : register(t14);
RaytracingAccelerationStructure             SceneBVH                    : register(t15);
RaytracingAccelerationStructure             PrevSceneBVH                : register(t16);
StructuredBuffer<PolymorphicLightInfo>      t_LightDataBuffer           : register(t17);
Texture2D<float4>                           t_GBufferEmissive           : register(t18);
StructuredBuffer<uint>                      t_GeometryInstanceToLight   : register(t19);
StructuredBuffer<PerInstanceData>           t_InstanceData              : register(t20);
StructuredBuffer<MeshData>                  t_GeometryData              : register(t21);
StructuredBuffer<MaterialConstants>         t_MaterialConstants         : register(t22);
StructuredBuffer<uint>                      t_SceneIndices              : register(t23);
StructuredBuffer<VertexQuantized>           t_SceneVertices             : register(t24);

// ---- UAVs (match RTXDIRenderer.cpp binding layout) ----
// u0  = u_LightReservoirs
// u1  = u_RisBuffer
// u2  = u_RisLightDataBuffer
// u3  = u_RestirLuminance         (stub)
// u4  = u_GIReservoirs            (stub)
// u5  = u_SecondaryGBuffer        (stub — future GI)
// u6  = u_DiffuseLighting
// u7  = u_SpecularLighting

RWStructuredBuffer<RTXDI_PackedDIReservoir> u_LightReservoirs           : register(u0);
RWStructuredBuffer<uint2>                   u_RisBuffer                 : register(u1);
RWStructuredBuffer<uint4>                   u_RisLightDataBuffer        : register(u2);
RWTexture2D<float2>                         u_RestirLuminance           : register(u3);
RWStructuredBuffer<RTXDI_PackedGIReservoir> u_GIReservoirs              : register(u4);
RWStructuredBuffer<SecondaryGBufferData>    u_SecondaryGBuffer          : register(u5);
RWTexture2D<float4>                         u_DiffuseLighting           : register(u6);
RWTexture2D<float4>                         u_SpecularLighting          : register(u7);

// RTXDI macro aliases
#define RTXDI_RIS_BUFFER                u_RisBuffer
#define RTXDI_LIGHT_RESERVOIR_BUFFER    u_LightReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER   t_NeighborOffsets
#define RTXDI_GI_RESERVOIR_BUFFER       u_GIReservoirs
#define IES_SAMPLER                     SamplerDescriptorHeap[SAMPLER_LINEAR_CLAMP_INDEX]

// Translates a light index between frames.
// Since lights don't stream in/out, the index is always stable — return it unchanged.
int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return int(lightIndex);
}

#endif // RAB_BUFFER_HLSLI