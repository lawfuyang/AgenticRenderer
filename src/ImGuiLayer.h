#pragma once

#include "pch.h"

class ImGuiLayer
{
public:
    void Initialize();
    void Shutdown();
    void ProcessEvent(const SDL_Event& event);
    void UpdateFrame();
};
