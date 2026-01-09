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

    bool SupportsGraphicsQueue(vk::PhysicalDevice device, uint32_t& outQueueFamily)
    {
        const std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queueFamilies.size(); ++i)
        {
            const vk::QueueFamilyProperties& qf = queueFamilies[i];
            if ((qf.queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlagBits{} && qf.queueCount > 0)
            {
                outQueueFamily = i;
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

    m_PhysicalDevice = ChoosePhysicalDevice();
    if (m_PhysicalDevice == VK_NULL_HANDLE)
    {
        return false;
    }

    return CreateLogicalDevice();
}

void GraphicRHI::Shutdown()
{
    DestroyLogicalDevice();
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

    m_Instance = static_cast<VkInstance>(instanceHandle);
    SDL_Log("[Init] Vulkan instance created successfully");
    return true;
}

bool GraphicRHI::CreateLogicalDevice()
{
    if (m_GraphicsQueueFamily == VK_QUEUE_FAMILY_IGNORED)
    {
        SDL_Log("Graphics queue family not set before logical device creation");
        SDL_assert(false && "Graphics queue family not set");
        return false;
    }

    vk::PhysicalDevice vkPhysical = static_cast<vk::PhysicalDevice>(m_PhysicalDevice);

    const float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.queueFamilyIndex = m_GraphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    vk::PhysicalDeviceFeatures deviceFeatures{};

    vk::DeviceCreateInfo createInfo{};
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;

    vk::Device vkDevice = vkPhysical.createDevice(createInfo);
    if (!vkDevice)
    {
        SDL_Log("vkCreateDevice failed");
        SDL_assert(false && "vkCreateDevice failed");
        return false;
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkDevice);

    m_Device = static_cast<VkDevice>(vkDevice);
    m_GraphicsQueue = static_cast<VkQueue>(vkDevice.getQueue(m_GraphicsQueueFamily, 0));

    SDL_Log("[Init] Vulkan logical device created (graphics family %u)", m_GraphicsQueueFamily);
    return true;
}

void GraphicRHI::DestroyInstance()
{
    if (m_Instance != VK_NULL_HANDLE)
    {
        SDL_Log("[Shutdown] Destroying Vulkan instance");
        vk::Instance vkInstance = static_cast<vk::Instance>(m_Instance);
        vkInstance.destroy();
        m_Instance = VK_NULL_HANDLE;
        m_PhysicalDevice = VK_NULL_HANDLE;
        m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        SDL_Vulkan_UnloadLibrary();
    }
}

void GraphicRHI::DestroyLogicalDevice()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        SDL_Log("[Shutdown] Destroying Vulkan logical device");
        vk::Device vkDevice = static_cast<vk::Device>(m_Device);
        vkDevice.destroy();
        m_Device = VK_NULL_HANDLE;
        m_GraphicsQueue = VK_NULL_HANDLE;
        m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    }
}

VkPhysicalDevice GraphicRHI::ChoosePhysicalDevice()
{
    SDL_Log("[Init] Selecting Vulkan physical device");

    vk::Instance vkInstance = static_cast<vk::Instance>(m_Instance);
    const std::vector<vk::PhysicalDevice> devices = vkInstance.enumeratePhysicalDevices();

    if (devices.empty())
    {
        SDL_Log("No Vulkan physical devices found");
        SDL_assert(false && "No Vulkan physical devices found");
        return VK_NULL_HANDLE;
    }

    vk::PhysicalDevice selected{};
    uint32_t selectedQueueFamily = VK_QUEUE_FAMILY_IGNORED;

    for (vk::PhysicalDevice device : devices)
    {
        uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
        if (!SupportsGraphicsQueue(device, graphicsFamily))
        {
            const vk::PhysicalDeviceProperties props = device.getProperties();
            SDL_Log("[Init] Skipping device without graphics queue: %s (%s)", props.deviceName, PhysicalDeviceTypeString(props.deviceType));
            continue;
        }

        const vk::PhysicalDeviceProperties props = device.getProperties();
        if (!selected)
        {
            selected = device;
            selectedQueueFamily = graphicsFamily;
        }

        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            selected = device;
            selectedQueueFamily = graphicsFamily;
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

    m_GraphicsQueueFamily = selectedQueueFamily;
    return static_cast<VkPhysicalDevice>(selected);
}
