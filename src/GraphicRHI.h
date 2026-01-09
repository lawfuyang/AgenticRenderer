#pragma once

#include "pch.h"

struct GraphicRHI
{
    bool Initialize();
    void Shutdown();

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;

private:
    bool CreateInstance();
    void DestroyInstance();
    VkPhysicalDevice ChoosePhysicalDevice();
    bool CreateLogicalDevice();
    void DestroyLogicalDevice();
};
