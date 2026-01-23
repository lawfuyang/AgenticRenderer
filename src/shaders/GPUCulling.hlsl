#define GPU_CULLING_DEFINE
#include "ShaderShared.h"

/*
	-- 2 Phase Occlusion Culling --

	Works under the assumption that it's likely that objects visible in the previous frame, will be visible this frame.

	In Phase 1, we render all objects that were visible last frame by testing against the previous HZB.
	Occluded objects are stored in a list, to be processed later.
	The HZB is constructed from the current result.
	Phase 2 tests all previously occluded objects against the new HZB and renders unoccluded.
	The HZB is constructed again from this result to be used in the next frame.

	https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf
*/

cbuffer CullingCB : register(b0)
{
    CullingConstants g_Culling;
};

StructuredBuffer<PerInstanceData> g_InstanceData : register(t0);
Texture2D<float> g_HZB : register(t1);
StructuredBuffer<MeshData> g_MeshData : register(t2);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_VisibleArgs : register(u0);
RWStructuredBuffer<uint> g_VisibleCount : register(u1);
RWStructuredBuffer<uint> g_OccludedIndices : register(u2);
RWStructuredBuffer<uint> g_OccludedCount : register(u3);
RWStructuredBuffer<DispatchIndirectArguments> g_DispatchIndirectArgs : register(u4);
SamplerState g_MinReductionSampler : register(s0);

bool FrustumSphereTest(
    float3 centerVS,
    float radius,
    float4 planes[5],   // view-space frustum planes
    float4x4 view
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

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
void ProjectSphereView(
    float3 c,          // sphere center in view space
    float  r,          // sphere radius
    float  zNear,      // near plane distance (> 0)
    float  P00,        // projection matrix [0][0]
    float  P11,        // projection matrix [1][1]
    out float4 aabb    // xy = min, zw = max (UV space)
)
{
    float3 cr = c * r;
    float  czr2 = c.z * c.z - r * r;

    // X bounds
    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    // Y bounds
    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    // NDC → clip-scaled
    aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);

    aabb.xy = clamp(aabb.xy, -1, 1);
    aabb.zw = clamp(aabb.zw, -1, 1);

    // Clip space [-1,1] → UV space [0,1]
    // note the lack of y flip due to Vulkan inverted viewport
    aabb = aabb.xwzy * float4(0.5f, 0.5f, 0.5f, 0.5f) + float4(0.5f, 0.5f, 0.5f, 0.5f);
}

bool OcclusionSphereTest(float3 center, float radius, uint2 HZBDims, float P00, float P11)
{
    // trivially accept if sphere intersects camera near plane
    if ((center.z - 0.1f) < radius)
        return true;
        
    float4 aabb;
    ProjectSphereView(center, radius, 0.1f, P00, P11, aabb);
    
	float width = (aabb.z - aabb.x) * HZBDims.x;
	float height = (aabb.w - aabb.y) * HZBDims.y;

	float level = floor(log2(max(width, height)));

    // Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
    float depthHZB = g_HZB.SampleLevel(g_MinReductionSampler, (aabb.xy + aabb.zw) * 0.5, level).r;

    float depthSphere = 0.1f / (center.z - radius); // reversed-Z, infinite far

    // visible if sphere is closer than occluder
    return depthSphere > depthHZB;
}

[numthreads(64, 1, 1)]
void Culling_CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint instanceIndex = dispatchThreadId.x;
	if (instanceIndex >= g_Culling.m_NumPrimitives)
		return;

	uint actualInstanceIndex = instanceIndex;
	if (g_Culling.m_Phase == 1)
	{
		// Phase 2: Only process instances that were occluded in Phase 1
		if (instanceIndex >= g_OccludedCount[0])
			return;
		actualInstanceIndex = g_OccludedIndices[instanceIndex];
	}

	PerInstanceData inst = g_InstanceData[actualInstanceIndex];
    MeshData mesh = g_MeshData[inst.m_MeshDataIndex];

    float3 sphereViewCenter = mul(float4(inst.m_Center, 1.0), g_Culling.m_View).xyz;

    bool isVisible = true;

    // Frustum culling
    if (g_Culling.m_EnableFrustumCulling)
    {
        isVisible &= FrustumSphereTest(sphereViewCenter, inst.m_Radius, g_Culling.m_FrustumPlanes, g_Culling.m_View);
    }

    // Occlusion culling
    if (g_Culling.m_EnableOcclusionCulling)
    {
        isVisible &= OcclusionSphereTest(sphereViewCenter, inst.m_Radius, uint2(g_Culling.m_HZBWidth, g_Culling.m_HZBHeight), g_Culling.m_P00, g_Culling.m_P11);
    }

    // Phase 1: Store visible instances for rendering occluded indices for Phase 2
    // Phase 2: Store visible instances for rendering newly tested visible instances against Phase 1 HZB
    if (isVisible)
    {
        uint visibleIndex;
        InterlockedAdd(g_VisibleCount[0], 1, visibleIndex);

        DrawIndexedIndirectArguments args;
        args.m_IndexCount = mesh.m_IndexCount;
        args.m_InstanceCount = 1;
        args.m_StartIndexLocation = mesh.m_IndexOffset;
        args.m_BaseVertexLocation = 0;
        args.m_StartInstanceLocation = actualInstanceIndex;

        g_VisibleArgs[visibleIndex] = args;
    }

    if (g_Culling.m_Phase == 0)
    {
        // Phase 1: Store visible instances for rendering occluded indices for Phase 2
        if (!isVisible)
        {
            uint occludedIndex;
            InterlockedAdd(g_OccludedCount[0], 1, occludedIndex);
            g_OccludedIndices[occludedIndex] = actualInstanceIndex;
        }
    }
}

[numthreads(1, 1, 1)]
void BuildIndirect_CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    g_DispatchIndirectArgs[0].m_ThreadGroupCountX = (g_OccludedCount[0] + 63) / 64;
    g_DispatchIndirectArgs[0].m_ThreadGroupCountY = 1;
    g_DispatchIndirectArgs[0].m_ThreadGroupCountZ = 1;
}
