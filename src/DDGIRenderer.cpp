#include "Renderer.h"
#include "CommonResources.h"

// ---------------------------------------------------------------------------
// DDGIRenderer — DDGI probe ray tracing and blending (RTXGI-DDGI)
//
// Phase 1: Skeleton only.  Setup() returns false when m_EnableDDGIProbeTracing is off,
//          so no GPU resources are allocated until DDGI is toggled on.
// ---------------------------------------------------------------------------
class DDGIRenderer : public IRenderer
{
public:
    bool Setup(RenderGraph& renderGraph) override
    {
        if (!g_Renderer.m_EnableDDGIProbeTracing)
            return false;

        // Future phases will declare render-graph resources here.
        return true;
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        // Future phases will dispatch probe tracing, SDK blending, and indirect query here.
    }

    const char* GetName() const override
    {
        return "DDGIRenderer";
    }
};

REGISTER_RENDERER(DDGIRenderer)
