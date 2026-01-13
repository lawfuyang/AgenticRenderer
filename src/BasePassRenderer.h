#pragma once

#include "pch.h"

class BasePassRenderer
{
public:
    bool Initialize();
    void Render(const nvrhi::CommandListHandle& commandList);

private:
    nvrhi::InputLayoutHandle m_InputLayout;
};
