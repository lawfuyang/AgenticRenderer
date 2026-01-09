#pragma once

#include "pch.h"
#include "GraphicRHI.h"
#include <nvrhi/vulkan.h>

struct Renderer
{
    SDL_Window* m_Window = nullptr;
    GraphicRHI m_RHI{};
    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::TextureHandle m_SwapchainTextures[GraphicRHI::SwapchainImageCount] = {};

    bool Initialize();
    void Run();
    void Shutdown();

private:
    bool CreateNvrhiDevice();
    void DestroyNvrhiDevice();
    bool CreateSwapchainTextures();
    void DestroySwapchainTextures();
};
