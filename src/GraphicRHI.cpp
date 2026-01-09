#include "GraphicRHI.h"
#include "Config.h"

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

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        (void)pUserData;

        const char* severityStr = "";
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            severityStr = "ERROR";
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            severityStr = "WARNING";
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            severityStr = "INFO";
        else
            severityStr = "VERBOSE";

        const char* typeStr = "";
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
            typeStr = "GENERAL";
        else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
            typeStr = "VALIDATION";
        else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
            typeStr = "PERFORMANCE";

        SDL_Log("[Vulkan %s][%s] %s (ID: %u)", severityStr, typeStr, pCallbackData->pMessage, pCallbackData->messageIdNumber);

        // Break in debugger on validation errors and warnings
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ||
            (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT))
        {
#ifdef _MSC_VER
            __debugbreak();
#else
            __builtin_trap();
#endif
        }

        return VK_FALSE;
    }
}


bool GraphicRHI::Initialize(SDL_Window* window)
{
    if (!CreateInstance())
    {
        return false;
    }

    if (!CreateSurface(window))
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
    DestroySwapchain();
    DestroyLogicalDevice();
    DestroySurface();
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

    // Add debug utils extension if validation is enabled
    const bool enableValidation = Config::Get().m_EnableGPUValidation;
    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Enable validation layers (modern unified layer)
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    if (enableValidation)
    {
        // Check if validation layers are available
        const std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();
        for (const char* layerName : validationLayers)
        {
            bool found = false;
            for (const vk::LayerProperties& layerProps : availableLayers)
            {
                if (std::strcmp(layerName, layerProps.layerName) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                SDL_Log("[Init] Validation layer not available: %s", layerName);
                SDL_assert(false && "Required validation layer not available");
                return false;
            }
        }

        SDL_Log("[Init] Enabling Vulkan validation layers");
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
    if (enableValidation)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

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

    if (!SetupDebugMessenger())
    {
        return false;
    }

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

    // Enable device extensions
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Enable device features
    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // Enable Vulkan 1.3 features
    vk::PhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;

    // Enable Vulkan 1.2 features
    vk::PhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.pNext = &vulkan13Features;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vulkan12Features.runtimeDescriptorArray = VK_TRUE;
    vulkan12Features.timelineSemaphore = VK_TRUE;

    vk::DeviceCreateInfo createInfo{};
    createInfo.pNext = &vulkan12Features;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
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

bool GraphicRHI::CreateSurface(SDL_Window* window)
{
    if (m_Surface != VK_NULL_HANDLE)
    {
        return true; // already created
    }

    if (!window || m_Instance == VK_NULL_HANDLE)
    {
        SDL_Log("SDL window or Vulkan instance not ready for surface creation");
        SDL_assert(false && "Invalid state for surface creation");
        return false;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, m_Instance, nullptr, &surface))
    {
        SDL_Log("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        SDL_assert(false && "SDL_Vulkan_CreateSurface failed");
        return false;
    }

    m_Surface = surface;
    SDL_Log("[Init] Vulkan surface created successfully");
    return true;
}

void GraphicRHI::DestroySurface()
{
    if (m_Surface != VK_NULL_HANDLE)
    {
        SDL_Log("[Shutdown] Destroying Vulkan surface");
        SDL_Vulkan_DestroySurface(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }
}

void GraphicRHI::DestroyInstance()
{
    DestroyDebugMessenger();

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

bool GraphicRHI::SetupDebugMessenger()
{
    if (!Config::Get().m_EnableGPUValidation)
    {
        return true;
    }

    vk::Instance vkInstance = static_cast<vk::Instance>(m_Instance);

    vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
    createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(&DebugCallback);

    try
    {
        m_DebugMessenger = static_cast<VkDebugUtilsMessengerEXT>(vkInstance.createDebugUtilsMessengerEXT(createInfo));
        SDL_Log("[Init] Debug messenger created successfully");
    }
    catch (const vk::SystemError& e)
    {
        SDL_Log("[Init] Failed to create debug messenger: %s", e.what());
        return false;
    }

    return true;
}

void GraphicRHI::DestroyDebugMessenger()
{
    if (m_DebugMessenger != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE)
    {
        SDL_Log("[Shutdown] Destroying debug messenger");
        vk::Instance vkInstance = static_cast<vk::Instance>(m_Instance);
        vkInstance.destroyDebugUtilsMessengerEXT(static_cast<vk::DebugUtilsMessengerEXT>(m_DebugMessenger));
        m_DebugMessenger = VK_NULL_HANDLE;
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

bool GraphicRHI::CreateSwapchain(uint32_t width, uint32_t height)
{
    if (m_Swapchain != VK_NULL_HANDLE)
    {
        SDL_Log("[Swapchain] Swapchain already created, destroying old one");
        DestroySwapchain();
    }

    if (m_Device == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE)
    {
        SDL_Log("[Swapchain] Cannot create swapchain: device, surface, or physical device not initialized");
        SDL_assert(false && "Invalid state for swapchain creation");
        return false;
    }

    SDL_Log("[Init] Creating Vulkan swapchain (size: %ux%u)", width, height);

    vk::PhysicalDevice vkPhysical = static_cast<vk::PhysicalDevice>(m_PhysicalDevice);
    vk::Device vkDevice = static_cast<vk::Device>(m_Device);
    vk::SurfaceKHR vkSurface = static_cast<vk::SurfaceKHR>(m_Surface);

    // Check if the graphics queue family supports presentation
    if (!SDL_Vulkan_GetPresentationSupport(m_Instance, m_PhysicalDevice, m_GraphicsQueueFamily))
    {
        SDL_Log("[Swapchain] Graphics queue family does not support presentation");
        SDL_assert(false && "Graphics queue family does not support presentation");
        return false;
    }

    // Query surface capabilities
    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = vkPhysical.getSurfaceCapabilitiesKHR(vkSurface);

    // Choose swap extent
    VkExtent2D swapExtent;
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
    {
        swapExtent = static_cast<VkExtent2D>(surfaceCapabilities.currentExtent);
    }
    else
    {
        swapExtent = {
            std::clamp(width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
            std::clamp(height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height)
        };
    }

    // Query surface formats
    const std::vector<vk::SurfaceFormatKHR> surfaceFormats = vkPhysical.getSurfaceFormatsKHR(vkSurface);
    if (surfaceFormats.empty())
    {
        SDL_Log("[Swapchain] No surface formats available");
        SDL_assert(false && "No surface formats available");
        return false;
    }

    // Prefer SRGB format if available
    vk::SurfaceFormatKHR selectedFormat = surfaceFormats[0];
    for (const vk::SurfaceFormatKHR& format : surfaceFormats)
    {
        if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            selectedFormat = format;
            break;
        }
    }

    SDL_Log("[Init] Swapchain format: %s, color space: %s", vk::to_string(selectedFormat.format).c_str(), vk::to_string(selectedFormat.colorSpace).c_str());

    // Determine present mode
    const std::vector<vk::PresentModeKHR> presentModes = vkPhysical.getSurfacePresentModesKHR(vkSurface);
    vk::PresentModeKHR selectedPresentMode = vk::PresentModeKHR::eMailbox;
    if (std::find(presentModes.begin(), presentModes.end(), vk::PresentModeKHR::eMailbox) == presentModes.end())
    {
        selectedPresentMode = vk::PresentModeKHR::eFifo; // always available
    }

    // Determine image count - we always want exactly 2 images
    constexpr uint32_t desiredImageCount = GraphicRHI::SwapchainImageCount;
    SDL_assert(surfaceCapabilities.minImageCount <= desiredImageCount && "Swapchain requires more than 2 images");
    SDL_assert(surfaceCapabilities.maxImageCount == 0 || surfaceCapabilities.maxImageCount >= desiredImageCount && "Swapchain cannot support 2 images");

    // Create swapchain
    vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.surface = vkSurface;
    swapchainCreateInfo.minImageCount = desiredImageCount;
    swapchainCreateInfo.imageFormat = selectedFormat.format;
    swapchainCreateInfo.imageColorSpace = selectedFormat.colorSpace;
    swapchainCreateInfo.imageExtent = static_cast<vk::Extent2D>(swapExtent);
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapchainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = nullptr;
    swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapchainCreateInfo.presentMode = selectedPresentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = nullptr;

    vk::SwapchainKHR vkSwapchain = vkDevice.createSwapchainKHR(swapchainCreateInfo);
    if (!vkSwapchain)
    {
        SDL_Log("[Swapchain] vkCreateSwapchainKHR failed");
        SDL_assert(false && "vkCreateSwapchainKHR failed");
        return false;
    }

    m_Swapchain = static_cast<VkSwapchainKHR>(vkSwapchain);
    m_SwapchainFormat = static_cast<VkFormat>(selectedFormat.format);
    m_SwapchainExtent = swapExtent;

    // Get swapchain images
    const std::vector<vk::Image> vkImages = vkDevice.getSwapchainImagesKHR(vkSwapchain);
    SDL_assert(vkImages.size() == SwapchainImageCount && "Swapchain must have exactly 2 images");
    for (size_t i = 0; i < SwapchainImageCount; ++i)
    {
        m_SwapchainImages[i] = static_cast<VkImage>(vkImages[i]);
    }

    SDL_Log("[Init] Swapchain created with %u images", SwapchainImageCount);

    // Create image views
    for (size_t i = 0; i < SwapchainImageCount; ++i)
    {
        vk::ImageViewCreateInfo imageViewCreateInfo{};
        imageViewCreateInfo.image = static_cast<vk::Image>(m_SwapchainImages[i]);
        imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
        imageViewCreateInfo.format = static_cast<vk::Format>(m_SwapchainFormat);
        imageViewCreateInfo.components = {
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity
        };
        imageViewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        vk::ImageView vkImageView = vkDevice.createImageView(imageViewCreateInfo);
        if (!vkImageView)
        {
            SDL_Log("[Swapchain] Failed to create image view %zu", i);
            SDL_assert(false && "createImageView failed");
            return false;
        }

        m_SwapchainImageViews[i] = static_cast<VkImageView>(vkImageView);
    }

    SDL_Log("[Init] Swapchain image views created successfully");
    return true;
}

void GraphicRHI::DestroySwapchain()
{
    if (m_Device == VK_NULL_HANDLE)
    {
        return; // device already destroyed
    }

    vk::Device vkDevice = static_cast<vk::Device>(m_Device);

    SDL_Log("[Shutdown] Destroying swapchain image views");
    for (size_t i = 0; i < SwapchainImageCount; ++i)
    {
        if (m_SwapchainImageViews[i] != VK_NULL_HANDLE)
        {
            vkDevice.destroyImageView(static_cast<vk::ImageView>(m_SwapchainImageViews[i]));
            m_SwapchainImageViews[i] = VK_NULL_HANDLE;
        }
    }

    if (m_Swapchain != VK_NULL_HANDLE)
    {
        SDL_Log("[Shutdown] Destroying Vulkan swapchain");
        vkDevice.destroySwapchainKHR(static_cast<vk::SwapchainKHR>(m_Swapchain));
        m_Swapchain = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < SwapchainImageCount; ++i)
    {
        m_SwapchainImages[i] = VK_NULL_HANDLE;
    }
    m_SwapchainFormat = VK_FORMAT_UNDEFINED;
    m_SwapchainExtent = {0, 0};
}
