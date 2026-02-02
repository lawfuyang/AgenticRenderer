#pragma once

#include "pch.h"

class ImGuiLayer
{
public:
    void Initialize();
    void Shutdown();
    void ProcessEvent(const SDL_Event& event);
    void UpdateFrame();

    float m_ExposureKeyValue = 0.18f;
    float m_AdaptationSpeed = 1.0f;
};
