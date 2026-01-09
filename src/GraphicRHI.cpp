#include "GraphicRHI.h"

// Define the Vulkan dynamic dispatcher - this needs to occur in exactly one cpp file in the program.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace
{
    const char* PhysicalDeviceTypeString(vk::PhysicalDeviceType type)
    {
        switch (type)
        {
        case vk::PhysicalDeviceType::eDiscreteGpu: return "Discrete GPU";
        case vk::PhysicalDeviceType::eIntegratedGpu: return "Integrated GPU";
        case vk::PhysicalDeviceType::eVirtualGpu: return "Virtual GPU";
        case vk::PhysicalDeviceType::eCpu: return "CPU";
        default: return "Other";
        }
    }

    bool SupportsGraphicsQueue(vk::PhysicalDevice device)
    {
        const std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();
        for (const vk::QueueFamilyProperties& qf : queueFamilies)
        {
            if ((qf.queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlagBits{} && qf.queueCount > 0)
            {
                return true;
            }
        }
        return false;
    }
}

bool GraphicRHI::Initialize()
{
    if (!CreateInstance())
    {
        return false;
    }

    vk::Instance vkInstance = static_cast<vk::Instance>(instance);
    physicalDevice = ChoosePhysicalDevice(vkInstance);
    return physicalDevice != VK_NULL_HANDLE;
}

void GraphicRHI::Shutdown()
{
    DestroyInstance();
}

bool GraphicRHI::CreateInstance()
{
    SDL_Log("[Init] Creating Vulkan instance");

    if (!SDL_Vulkan_LoadLibrary(nullptr))
    {
        SDL_Log("SDL_Vulkan_LoadLibrary failed: %s", SDL_GetError());
        SDL_assert(false && "SDL_Vulkan_LoadLibrary failed");
        return false;
    }

    vk::detail::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions)
    {
        SDL_Log("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
        SDL_assert(false && "SDL_Vulkan_GetInstanceExtensions failed");
        return false;
    }

    SDL_Log("[Init] SDL3 requires %u Vulkan instance extensions:", sdlExtensionCount);
    std::vector<const char*> extensions;
    extensions.reserve(sdlExtensionCount);
    for (uint32_t i = 0; i < sdlExtensionCount; ++i)
    {
        SDL_Log("[Init]   - %s", sdlExtensions[i]);
        extensions.push_back(sdlExtensions[i]);
    }

    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "Agentic Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = 0;

    vk::Instance instanceHandle = vk::createInstance(createInfo);
    if (!instanceHandle)
    {
        SDL_Log("vkCreateInstance failed");
        SDL_assert(false && "vkCreateInstance failed");
        return false;
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(instanceHandle);

    instance = static_cast<VkInstance>(instanceHandle);
    SDL_Log("[Init] Vulkan instance created successfully");
    return true;
}

void GraphicRHI::DestroyInstance()
{
    if (instance != VK_NULL_HANDLE)
    {
        SDL_Log("[Shutdown] Destroying Vulkan instance");
        vk::Instance vkInstance = static_cast<vk::Instance>(instance);
        vkInstance.destroy();
        instance = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
        SDL_Vulkan_UnloadLibrary();
    }
}

VkPhysicalDevice GraphicRHI::ChoosePhysicalDevice(vk::Instance instanceHandle)
{
    SDL_Log("[Init] Selecting Vulkan physical device");

    const std::vector<vk::PhysicalDevice> devices = instanceHandle.enumeratePhysicalDevices();

    if (devices.empty())
    {
        SDL_Log("No Vulkan physical devices found");
        SDL_assert(false && "No Vulkan physical devices found");
        return VK_NULL_HANDLE;
    }

    vk::PhysicalDevice selected{};

    for (vk::PhysicalDevice device : devices)
    {
        if (!SupportsGraphicsQueue(device))
        {
            const vk::PhysicalDeviceProperties props = device.getProperties();
            SDL_Log("[Init] Skipping device without graphics queue: %s (%s)", props.deviceName, PhysicalDeviceTypeString(props.deviceType));
            continue;
        }

        const vk::PhysicalDeviceProperties props = device.getProperties();
        if (!selected)
        {
            selected = device;
        }

        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            selected = device;
            break; // prefer the first discrete GPU we find
        }
    }

    if (!selected)
    {
        SDL_Log("No suitable Vulkan physical device with a graphics queue was found");
        SDL_assert(false && "No suitable Vulkan physical device found");
        return VK_NULL_HANDLE;
    }

    const vk::PhysicalDeviceProperties chosenProps = selected.getProperties();
    SDL_Log("[Init] Selected device: %s (%s)", chosenProps.deviceName, PhysicalDeviceTypeString(chosenProps.deviceType));

    return static_cast<VkPhysicalDevice>(selected);
}
