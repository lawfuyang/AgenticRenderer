#ifndef COMMON_LIGHTING_HLSLI
#define COMMON_LIGHTING_HLSLI

#include "ShaderShared.h"

// Octahedral encoding for normals
// From: http://jcgt.org/published/0003/02/01/
float2 octWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0);
}

float2 EncodeNormal(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : octWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 DecodeNormal(float2 f)
{
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

// PBR Helper functions
float3 ComputeF0(float3 baseColor, float metallic)
{
    return lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
}

float D_GGX(float NdotH, float m)
{
    float m2 = m * m;
    float f = (NdotH * m2 - NdotH) * NdotH + 1.0;
    return m2 / (3.14159265 * f * f);
}

float Vis_SmithJointApprox(float a2, float NdotV, float NdotL)
{
    float visV = NdotL * sqrt(NdotV * (NdotV - a2 * NdotV) + a2);
    float visL = NdotV * sqrt(NdotL * (NdotL - a2 * NdotL) + a2);
    return 0.5 / (visV + visL);
}

float3 F_Schlick(float3 f0, float VdotH)
{
    float f90 = saturate(dot(f0, 50.0 * 0.33));
    return f0 + (f90 - f0) * pow(1.0 - VdotH, 5.0);
}

float OrenNayar(float nl, float nv, float lv, float a2, float s)
{
    float A = 1.0 - 0.5 * a2 / (a2 + 0.33);
    float B = 0.45 * a2 / (a2 + 0.09);
    float C = sqrt((1.0 - nl * nl) * (1.0 - nv * nv)) / max(nl, nv);
    float cos_phi_diff = lv - nl * nv;
    return s * nl * (A + B * max(0.0, cos_phi_diff) * C);
}

struct LightingInputs
{
    float3 N;
    float3 V;
    float3 L;
    float3 baseColor;
    float roughness;
    float metallic;
    float lightIntensity;
    float3 worldPos;
    bool enableRTShadows;
    RaytracingAccelerationStructure sceneAS;
    StructuredBuffer<PerInstanceData> instances;
    StructuredBuffer<MeshData> meshData;
    StructuredBuffer<MaterialConstants> materials;
    StructuredBuffer<uint> indices;
    StructuredBuffer<VertexQuantized> vertices;
    SamplerState clampSampler;
    SamplerState wrapSampler;
};

float3 ComputeDirectionalLighting(LightingInputs inputs)
{
    float3 H = normalize(inputs.V + inputs.L);

    float NdotL = saturate(dot(inputs.N, inputs.L));
    float NdotV = saturate(dot(inputs.N, inputs.V));
    float NdotH = saturate(dot(inputs.N, H));
    float VdotH = saturate(dot(inputs.V, H));
    float LdotV = saturate(dot(inputs.L, inputs.V));

    float a2 = clamp(inputs.roughness * inputs.roughness * inputs.roughness * inputs.roughness, 0.0001f, 1.0f);
    float oren = OrenNayar(NdotL, NdotV, LdotV, a2, 1.0f);
    float3 kD = (1.0 - inputs.metallic);
    float3 diffuse = oren * kD * inputs.baseColor;

    float3 f0 = ComputeF0(inputs.baseColor, inputs.metallic);
    float D = D_GGX(a2, NdotH);
    float Vis = Vis_SmithJointApprox(a2, NdotV, NdotL);
    float3 F = F_Schlick(f0, VdotH);
    float3 spec = (D * Vis) * F;

    float3 radiance = float3(inputs.lightIntensity, inputs.lightIntensity, inputs.lightIntensity);

    // Raytraced shadows
    float shadow = 1.0f;
    if (inputs.enableRTShadows)
    {
        RayDesc ray;
        ray.Origin = inputs.worldPos + inputs.N * 0.1f;
        ray.Direction = inputs.L;
        ray.TMin = 0.1f;
        ray.TMax = 1e10f;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        q.TraceRayInline(inputs.sceneAS, RAY_FLAG_NONE, 0xFF, ray);
        
        while (q.Proceed())
        {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
            {
                uint instanceIndex = q.CandidateInstanceIndex();
                uint primitiveIndex = q.CandidatePrimitiveIndex();
                float2 bary = q.CandidateTriangleBarycentrics();

                PerInstanceData inst = inputs.instances[instanceIndex];
                MeshData mesh = inputs.meshData[inst.m_MeshDataIndex];
                MaterialConstants mat = inputs.materials[inst.m_MaterialIndex];

                if (mat.m_AlphaMode == ALPHA_MODE_MASK)
                {
                    uint baseIndex = mesh.m_IndexOffsets[0];
                    uint i0 = inputs.indices[baseIndex + 3 * primitiveIndex + 0];
                    uint i1 = inputs.indices[baseIndex + 3 * primitiveIndex + 1];
                    uint i2 = inputs.indices[baseIndex + 3 * primitiveIndex + 2];

                    Vertex v0 = UnpackVertex(inputs.vertices[i0]);
                    Vertex v1 = UnpackVertex(inputs.vertices[i1]);
                    Vertex v2 = UnpackVertex(inputs.vertices[i2]);

                    float2 uv0 = v0.m_Uv;
                    float2 uv1 = v1.m_Uv;
                    float2 uv2 = v2.m_Uv;

                    float2 uv = uv0 * (1.0f - bary.x - bary.y) + uv1 * bary.x + uv2 * bary.y;
                    
                    // Compute approximate UV gradients for proper texture filtering in ray tracing
                    // Use triangle geometry and distance to estimate ddx/ddy
                    float3 p0 = mul(float4(v0.m_Pos, 1.0f), inst.m_World).xyz;
                    float3 p1 = mul(float4(v1.m_Pos, 1.0f), inst.m_World).xyz;
                    float3 p2 = mul(float4(v2.m_Pos, 1.0f), inst.m_World).xyz;
                    
                    // Interpolate hit position using barycentrics
                    float3 hitPos = p0 * (1.0f - bary.x - bary.y) + p1 * bary.x + p2 * bary.y;
                    float dist = length(hitPos - ray.Origin);
                    
                    // Compute triangle area in world space
                    float3 edge1 = p1 - p0;
                    float3 edge2 = p2 - p0;
                    float triangleArea = length(cross(edge1, edge2)) * 0.5f;
                    
                    // Compute UV range across the triangle
                    float2 uvMin = min(uv0, min(uv1, uv2));
                    float2 uvMax = max(uv0, max(uv1, uv2));
                    float2 uvRange = uvMax - uvMin;
                    
                    // Approximate gradient scale based on triangle size and distance
                    // This provides a heuristic for proper mip selection
                    float gradientScale = triangleArea / max(dist, 0.1f);
                    float2 ddx_uv = uvRange * gradientScale;
                    float2 ddy_uv = uvRange * gradientScale;
                    
                    bool hasAlbedo = (mat.m_TextureFlags & TEXFLAG_ALBEDO) != 0;
                    float4 albedoSample = hasAlbedo 
                        ? SampleBindlessTextureGrad(mat.m_AlbedoTextureIndex, inputs.clampSampler, inputs.wrapSampler, mat.m_AlbedoSamplerIndex, uv, ddx_uv, ddy_uv)
                        : float4(mat.m_BaseColor.xyz, mat.m_BaseColor.w);
                    
                    float alpha = hasAlbedo ? (albedoSample.w * mat.m_BaseColor.w) : mat.m_BaseColor.w;
                    
                    if (alpha >= mat.m_AlphaCutoff)
                    {
                        q.CommitNonOpaqueTriangleHit();
                    }
                }
                else if (mat.m_AlphaMode == ALPHA_MODE_BLEND)
                {
                    // ignore. dont let transparent geometry cast shadows
                }
                else if (mat.m_AlphaMode == ALPHA_MODE_OPAQUE)
                {
                    // This should not happen if TLAS/BLAS flags are correct, but handle it just in case
                    q.CommitNonOpaqueTriangleHit();
                }
            }
        }

        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            shadow = 0.0f;
        }
    }

    return (diffuse + spec) * radiance * NdotL * shadow;
}

#endif // COMMON_LIGHTING_HLSLI
