#pragma once

#include "pch.h"
#include "GraphicRHI.h"

struct Renderer
{
    SDL_Window* m_Window = nullptr;
    GraphicRHI m_RHI{};
    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::TextureHandle m_SwapchainTextures[GraphicRHI::SwapchainImageCount] = {};

    // ImGui state
    double m_FrameTime = 0.0;
    double m_FPS = 0.0;

    bool Initialize();
    void Run();
    void Shutdown();

private:
    bool CreateNvrhiDevice();
    void DestroyNvrhiDevice();
    bool CreateSwapchainTextures();
    void DestroySwapchainTextures();
    
    bool InitializeImGui();
    void ShutdownImGui();
    void RenderImGuiFrame();
};
