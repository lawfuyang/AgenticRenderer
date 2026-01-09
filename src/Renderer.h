#pragma once

#include "pch.h"
#include "GraphicRHI.h"

struct Renderer
{
    SDL_Window* m_Window = nullptr;
    GraphicRHI m_RHI{};

    bool Initialize();
    void Run();
    void Shutdown();
};
