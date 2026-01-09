#pragma once

#include "pch.h"

struct GraphicRHI
{
    bool Initialize();
    void Shutdown();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

private:
    bool CreateInstance();
    void DestroyInstance();
    VkPhysicalDevice ChoosePhysicalDevice(vk::Instance instanceHandle);
};
