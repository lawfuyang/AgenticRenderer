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
    uint g_NumPrimitives;
};

StructuredBuffer<PerInstanceData> g_InstanceData : register(t0);
RWStructuredBuffer<DrawIndexedIndirectArguments> g_IndirectArgs : register(u0);

[numthreads(64, 1, 1)]
void Culling_CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint instanceIndex = dispatchThreadId.x;
	if (instanceIndex >= g_NumPrimitives)
		return;

	PerInstanceData inst = g_InstanceData[instanceIndex];

	DrawIndexedIndirectArguments args;
	args.m_IndexCount = inst.m_IndexCount;
	args.m_InstanceCount = 1;
	args.m_StartIndexLocation = inst.m_IndexOffset;
	args.m_BaseVertexLocation = 0;
	args.m_StartInstanceLocation = instanceIndex;

	g_IndirectArgs[instanceIndex] = args;
}
