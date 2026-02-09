#include "Renderer.h"
#include "Config.h"
#include "Utilities.h"
#include "CommonResources.h"
#include "shaders/ShaderShared.h"

static constexpr uint32_t kBloomMipCount = 6;

class BloomRenderer : public IRenderer
{
public:
    
    void Initialize() override
    {
        Renderer* renderer = Renderer::GetInstance();
        nvrhi::DeviceHandle device = renderer->m_RHI->m_NvrhiDevice;

        const uint32_t width = renderer->m_RHI->m_SwapchainExtent.x;
        const uint32_t height = renderer->m_RHI->m_SwapchainExtent.y;

        // Create Bloom textures
        nvrhi::TextureDesc desc;
        desc.width = width / 2;
        desc.height = height / 2;
        desc.format = nvrhi::Format::R11G11B10_FLOAT;
        desc.mipLevels = kBloomMipCount;
        desc.isRenderTarget = true;
        desc.debugName = "Bloom_DownPyramid";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.setClearValue(nvrhi::Color{});
        renderer->m_BloomDownPyramid = device->createTexture(desc);

        desc.debugName = "Bloom_UpPyramid";
        renderer->m_BloomUpPyramid = device->createTexture(desc);
    }

    void PostSceneLoad() override {}

    void Render(nvrhi::CommandListHandle commandList) override
    {
        Renderer* renderer = Renderer::GetInstance();
        nvrhi::DeviceHandle device = renderer->m_RHI->m_NvrhiDevice;
        if (!renderer->m_EnableBloom) return;

        nvrhi::utils::ScopedMarker marker(commandList, "Bloom");

        const uint32_t width = renderer->m_RHI->m_SwapchainExtent.x;
        const uint32_t height = renderer->m_RHI->m_SwapchainExtent.y;

        // 1. Prefilter (HDR -> Down[0])
        {
            BloomConstants consts{};
            consts.m_Knee = renderer->m_BloomKnee;
            consts.m_Strength = 1.0f;
            consts.m_Width = width / 2;
            consts.m_Height = height / 2;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(BloomConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, renderer->m_HDRColorTexture),
                nvrhi::BindingSetItem::Sampler(0, CommonResources::GetInstance().LinearClamp)
            };

            nvrhi::FramebufferHandle fb = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(renderer->m_BloomDownPyramid, nvrhi::TextureSubresourceSet(0, 1, 0, 1)));

            commandList->clearTextureFloat(renderer->m_BloomDownPyramid, nvrhi::TextureSubresourceSet(0, 1, 0, 1), nvrhi::Color(0.f, 0.f, 0.f, 0.f));

            Renderer::RenderPassParams params{
                .commandList = commandList,
                .shaderName = "Bloom_Prefilter_PSMain",
                .bindingSetDesc = bset,
                .pushConstants = &consts,
                .pushConstantsSize = sizeof(consts),
                .framebuffer = fb
            };
            renderer->AddFullScreenPass(params);
        }

        // 2. Downsample chain (Down[i-1] -> Down[i])
        for (uint32_t i = 1; i < kBloomMipCount; ++i)
        {
            uint32_t mipW = (width / 2) >> i;
            uint32_t mipH = (height / 2) >> i;
            if (mipW == 0 || mipH == 0) break;

            BloomConstants consts{};
            consts.m_Width = mipW;
            consts.m_Height = mipH;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(BloomConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, renderer->m_BloomDownPyramid, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(i - 1, 1, 0, 1)),
                nvrhi::BindingSetItem::Sampler(0, CommonResources::GetInstance().LinearClamp)
            };

            nvrhi::FramebufferHandle fb = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(renderer->m_BloomDownPyramid, nvrhi::TextureSubresourceSet(i, 1, 0, 1)));

            commandList->clearTextureFloat(renderer->m_BloomDownPyramid, nvrhi::TextureSubresourceSet(i, 1, 0, 1), nvrhi::Color(0.f, 0.f, 0.f, 0.f));

            Renderer::RenderPassParams params{
                .commandList = commandList,
                .shaderName = "Bloom_Downsample_PSMain",
                .bindingSetDesc = bset,
                .pushConstants = &consts,
                .pushConstantsSize = sizeof(consts),
                .framebuffer = fb
            };
            renderer->AddFullScreenPass(params);
        }

        // 3. Upsample chain (Up[i+1] + Down[i] -> Up[i])
        // Seed the up-chain with the smallest down mip

        nvrhi::TextureSlice slice;
        slice.mipLevel = kBloomMipCount - 1;

        commandList->copyTexture(renderer->m_BloomUpPyramid, slice, renderer->m_BloomDownPyramid, slice);

        for (int i = kBloomMipCount - 2; i >= 0; --i)
        {
            uint32_t mipW = (width / 2) >> i;
            uint32_t mipH = (height / 2) >> i;

            BloomConstants consts{};
            consts.m_Width = mipW;
            consts.m_Height = mipH;
            consts.m_UpsampleRadius = renderer->m_UpsampleRadius;

            nvrhi::BindingSetDesc bset;
            bset.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(BloomConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, renderer->m_BloomUpPyramid,   nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(i + 1, 1, 0, 1)),
                nvrhi::BindingSetItem::Texture_SRV(1, renderer->m_BloomDownPyramid, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(i,     1, 0, 1)),
                nvrhi::BindingSetItem::Sampler(0, CommonResources::GetInstance().LinearClamp)
            };

            nvrhi::FramebufferHandle fb = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(renderer->m_BloomUpPyramid, nvrhi::TextureSubresourceSet(i, 1, 0, 1)));

            commandList->clearTextureFloat(renderer->m_BloomUpPyramid, nvrhi::TextureSubresourceSet(i, 1, 0, 1), nvrhi::Color(0.f, 0.f, 0.f, 0.f));

            Renderer::RenderPassParams params{
                .commandList = commandList,
                .shaderName = "Bloom_Upsample_PSMain",
                .bindingSetDesc = bset,
                .pushConstants = &consts,
                .pushConstantsSize = sizeof(consts),
                .framebuffer = fb
            };
            renderer->AddFullScreenPass(params);
        }
    }

    const char* GetName() const override { return "Bloom"; }
};

REGISTER_RENDERER(BloomRenderer);
