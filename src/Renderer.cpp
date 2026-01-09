#include "Renderer.h"
#include "Utilities.h"
#include "Config.h"

namespace
{
    nvrhi::Format VkFormatToNvrhiFormat(VkFormat vkFormat)
    {
        switch (vkFormat)
        {
        case VK_FORMAT_UNDEFINED:                      return nvrhi::Format::UNKNOWN;
        case VK_FORMAT_R8_UINT:                        return nvrhi::Format::R8_UINT;
        case VK_FORMAT_R8_SINT:                        return nvrhi::Format::R8_SINT;
        case VK_FORMAT_R8_UNORM:                       return nvrhi::Format::R8_UNORM;
        case VK_FORMAT_R8_SNORM:                       return nvrhi::Format::R8_SNORM;
        case VK_FORMAT_R8G8_UINT:                      return nvrhi::Format::RG8_UINT;
        case VK_FORMAT_R8G8_SINT:                      return nvrhi::Format::RG8_SINT;
        case VK_FORMAT_R8G8_UNORM:                     return nvrhi::Format::RG8_UNORM;
        case VK_FORMAT_R8G8_SNORM:                     return nvrhi::Format::RG8_SNORM;
        case VK_FORMAT_R16_UINT:                       return nvrhi::Format::R16_UINT;
        case VK_FORMAT_R16_SINT:                       return nvrhi::Format::R16_SINT;
        case VK_FORMAT_R16_UNORM:                      return nvrhi::Format::R16_UNORM;
        case VK_FORMAT_R16_SNORM:                      return nvrhi::Format::R16_SNORM;
        case VK_FORMAT_R16_SFLOAT:                     return nvrhi::Format::R16_FLOAT;
        case VK_FORMAT_A4R4G4B4_UNORM_PACK16:          return nvrhi::Format::BGRA4_UNORM;
        case VK_FORMAT_B5G6R5_UNORM_PACK16:            return nvrhi::Format::B5G6R5_UNORM;
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16:          return nvrhi::Format::B5G5R5A1_UNORM;
        case VK_FORMAT_R8G8B8A8_UINT:                  return nvrhi::Format::RGBA8_UINT;
        case VK_FORMAT_R8G8B8A8_SINT:                  return nvrhi::Format::RGBA8_SINT;
        case VK_FORMAT_R8G8B8A8_UNORM:                 return nvrhi::Format::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SNORM:                 return nvrhi::Format::RGBA8_SNORM;
        case VK_FORMAT_B8G8R8A8_UNORM:                 return nvrhi::Format::BGRA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:                  return nvrhi::Format::SRGBA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:                  return nvrhi::Format::SBGRA8_UNORM;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:       return nvrhi::Format::R10G10B10A2_UNORM;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:        return nvrhi::Format::R11G11B10_FLOAT;
        case VK_FORMAT_R16G16_UINT:                    return nvrhi::Format::RG16_UINT;
        case VK_FORMAT_R16G16_SINT:                    return nvrhi::Format::RG16_SINT;
        case VK_FORMAT_R16G16_UNORM:                   return nvrhi::Format::RG16_UNORM;
        case VK_FORMAT_R16G16_SNORM:                   return nvrhi::Format::RG16_SNORM;
        case VK_FORMAT_R16G16_SFLOAT:                  return nvrhi::Format::RG16_FLOAT;
        case VK_FORMAT_R32_UINT:                       return nvrhi::Format::R32_UINT;
        case VK_FORMAT_R32_SINT:                       return nvrhi::Format::R32_SINT;
        case VK_FORMAT_R32_SFLOAT:                     return nvrhi::Format::R32_FLOAT;
        case VK_FORMAT_R16G16B16A16_UINT:              return nvrhi::Format::RGBA16_UINT;
        case VK_FORMAT_R16G16B16A16_SINT:              return nvrhi::Format::RGBA16_SINT;
        case VK_FORMAT_R16G16B16A16_SFLOAT:            return nvrhi::Format::RGBA16_FLOAT;
        case VK_FORMAT_R16G16B16A16_UNORM:             return nvrhi::Format::RGBA16_UNORM;
        case VK_FORMAT_R16G16B16A16_SNORM:             return nvrhi::Format::RGBA16_SNORM;
        case VK_FORMAT_R32G32_UINT:                    return nvrhi::Format::RG32_UINT;
        case VK_FORMAT_R32G32_SINT:                    return nvrhi::Format::RG32_SINT;
        case VK_FORMAT_R32G32_SFLOAT:                  return nvrhi::Format::RG32_FLOAT;
        case VK_FORMAT_R32G32B32_UINT:                 return nvrhi::Format::RGB32_UINT;
        case VK_FORMAT_R32G32B32_SINT:                 return nvrhi::Format::RGB32_SINT;
        case VK_FORMAT_R32G32B32_SFLOAT:               return nvrhi::Format::RGB32_FLOAT;
        case VK_FORMAT_R32G32B32A32_UINT:              return nvrhi::Format::RGBA32_UINT;
        case VK_FORMAT_R32G32B32A32_SINT:              return nvrhi::Format::RGBA32_SINT;
        case VK_FORMAT_R32G32B32A32_SFLOAT:            return nvrhi::Format::RGBA32_FLOAT;
        case VK_FORMAT_D16_UNORM:                      return nvrhi::Format::D16;
        case VK_FORMAT_D24_UNORM_S8_UINT:             return nvrhi::Format::D24S8;
        case VK_FORMAT_D32_SFLOAT:                     return nvrhi::Format::D32;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:            return nvrhi::Format::D32S8;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:          return nvrhi::Format::BC1_UNORM;
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:           return nvrhi::Format::BC1_UNORM_SRGB;
        case VK_FORMAT_BC2_UNORM_BLOCK:               return nvrhi::Format::BC2_UNORM;
        case VK_FORMAT_BC2_SRGB_BLOCK:                return nvrhi::Format::BC2_UNORM_SRGB;
        case VK_FORMAT_BC3_UNORM_BLOCK:               return nvrhi::Format::BC3_UNORM;
        case VK_FORMAT_BC3_SRGB_BLOCK:                return nvrhi::Format::BC3_UNORM_SRGB;
        case VK_FORMAT_BC4_UNORM_BLOCK:               return nvrhi::Format::BC4_UNORM;
        case VK_FORMAT_BC4_SNORM_BLOCK:               return nvrhi::Format::BC4_SNORM;
        case VK_FORMAT_BC5_UNORM_BLOCK:               return nvrhi::Format::BC5_UNORM;
        case VK_FORMAT_BC5_SNORM_BLOCK:               return nvrhi::Format::BC5_SNORM;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:             return nvrhi::Format::BC6H_UFLOAT;
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:             return nvrhi::Format::BC6H_SFLOAT;
        case VK_FORMAT_BC7_UNORM_BLOCK:               return nvrhi::Format::BC7_UNORM;
        case VK_FORMAT_BC7_SRGB_BLOCK:                return nvrhi::Format::BC7_UNORM_SRGB;
        default:
            SDL_Log("[Warning] Unsupported VkFormat: %d", vkFormat);
            return nvrhi::Format::UNKNOWN;
        }
    }

    void HandleInput(const SDL_Event& event)
    {
        (void)event;
    }

    void InitSDL()
    {
        SDL_Log("[Init] Starting SDL initialization");
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            SDL_assert(false && "SDL_Init failed");
            return;
        }
        SDL_Log("[Init] SDL initialized");
    }

    void ChooseWindowSize(int* outWidth, int* outHeight)
    {
        int windowW = 1280;
        int windowH = 720;

        const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
        SDL_Rect usableBounds{};
        if (!SDL_GetDisplayUsableBounds(primaryDisplay, &usableBounds))
        {
            SDL_Log("SDL_GetDisplayUsableBounds failed: %s", SDL_GetError());
            SDL_assert(false && "SDL_GetDisplayUsableBounds failed");
            *outWidth = windowW;
            *outHeight = windowH;
            return;
        }

        int maxFitW = usableBounds.w;
        int maxFitH = usableBounds.h;
        if (static_cast<int64_t>(maxFitW) * 9 > static_cast<int64_t>(maxFitH) * 16)
        {
            maxFitW = (maxFitH * 16) / 9;
        }
        else
        {
            maxFitH = (maxFitW * 9) / 16;
        }

        constexpr int kStandard16x9[][2] = {
            {3840, 2160},
            {2560, 1440},
            {1920, 1080},
            {1600, 900},
            {1280, 720},
        };
        constexpr int kStandard16x9Count = static_cast<int>(sizeof(kStandard16x9) / sizeof(kStandard16x9[0]));

        windowW = maxFitW;
        windowH = maxFitH;
        int firstFitIndex = -1;
        for (int i = 0; i < kStandard16x9Count; ++i)
        {
            if (kStandard16x9[i][0] <= maxFitW && kStandard16x9[i][1] <= maxFitH)
            {
                firstFitIndex = i;
                break;
            }
        }

        if (firstFitIndex >= 0)
        {
            int chosenIndex = firstFitIndex;
            const bool fillsUsableWidth  = kStandard16x9[firstFitIndex][0] == maxFitW;
            const bool fillsUsableHeight = kStandard16x9[firstFitIndex][1] == maxFitH;
            if (fillsUsableWidth && fillsUsableHeight && firstFitIndex + 1 < kStandard16x9Count)
            {
                chosenIndex = firstFitIndex + 1;
            }

            windowW = kStandard16x9[chosenIndex][0];
            windowH = kStandard16x9[chosenIndex][1];
        }

        SDL_Log("[Init] Usable bounds: %dx%d, max 16:9 fit: %dx%d, chosen: %dx%d", usableBounds.w, usableBounds.h, maxFitW, maxFitH, windowW, windowH);
        *outWidth = windowW;
        *outHeight = windowH;
    }

    SDL_Window* CreateWindowScaled()
    {
        int windowW = 0;
        int windowH = 0;
        ChooseWindowSize(&windowW, &windowH);

        SDL_Log("[Init] Creating window");
        SDL_Window* window = SDL_CreateWindow("Agentic Renderer", windowW, windowH, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window)
        {
            SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_assert(false && "SDL_CreateWindow failed");
            return nullptr;
        }

        SDL_Log("[Init] Window created (%dx%d)", windowW, windowH);
        return window;
    }
}

bool Renderer::Initialize()
{
    ScopedTimerLog initScope{"[Timing] Init phase:"};

    InitSDL();

    m_Window = CreateWindowScaled();
    if (!m_Window)
    {
        SDL_Quit();
        return false;
    }

    if (!m_RHI.Initialize(m_Window))
    {
        Shutdown();
        return false;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

    if (!m_RHI.CreateSwapchain(static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)))
    {
        Shutdown();
        return false;
    }

    if (!CreateNvrhiDevice())
    {
        Shutdown();
        return false;
    }

    if (!CreateSwapchainTextures())
    {
        Shutdown();
        return false;
    }

    return true;
}

void Renderer::Run()
{
    ScopedTimerLog runScope{"[Timing] Run phase:"};

    SDL_Log("[Run ] Entering main loop");

    constexpr uint32_t kTargetFPS = 200;
    constexpr uint32_t kFrameDurationMs = SDL_MS_PER_SECOND / kTargetFPS;

    bool running = true;
    while (running)
    {
        const uint64_t frameStart = SDL_GetTicks();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                SDL_Log("[Run ] Received quit event");
                running = false;
                break;
            }

            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                HandleInput(event);
                break;
            default:
                break;
            }
        }

        const uint64_t frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < kFrameDurationMs)
        {
            SDL_Delay(static_cast<uint32_t>(kFrameDurationMs - frameTime));
        }
    }
}

void Renderer::Shutdown()
{
    ScopedTimerLog shutdownScope{"[Timing] Shutdown phase:"};

    DestroySwapchainTextures();
    DestroyNvrhiDevice();
    m_RHI.Shutdown();

    if (m_Window)
    {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }

    SDL_Quit();
    SDL_Log("[Shutdown] Clean exit");
}

bool Renderer::CreateNvrhiDevice()
{
    SDL_Log("[Init] Creating NVRHI Vulkan device");

    nvrhi::vulkan::DeviceDesc deviceDesc;
    deviceDesc.instance = static_cast<VkInstance>(m_RHI.m_Instance);
    deviceDesc.physicalDevice = m_RHI.m_PhysicalDevice;
    deviceDesc.device = m_RHI.m_Device;
    deviceDesc.graphicsQueue = m_RHI.m_GraphicsQueue;
    deviceDesc.graphicsQueueIndex = m_RHI.m_GraphicsQueueFamily;

    m_NvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);
    if (!m_NvrhiDevice)
    {
        SDL_Log("[Init] Failed to create NVRHI device");
        SDL_assert(false && "Failed to create NVRHI device");
        return false;
    }

    SDL_Log("[Init] NVRHI Vulkan device created successfully");

    // Wrap with validation layer if enabled
    if (Config::Get().m_EnableGPUValidation)
    {
        SDL_Log("[Init] Wrapping device with NVRHI validation layer");
        m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
        SDL_Log("[Init] NVRHI validation layer enabled");
    }

    return true;
}

void Renderer::DestroyNvrhiDevice()
{
    if (m_NvrhiDevice)
    {
        SDL_Log("[Shutdown] Destroying NVRHI device");
        m_NvrhiDevice = nullptr;
    }
}

bool Renderer::CreateSwapchainTextures()
{
    SDL_Log("[Init] Creating NVRHI swap chain texture handles");

    // Convert VkFormat to nvrhi::Format
    const nvrhi::Format nvrhiFormat = VkFormatToNvrhiFormat(m_RHI.m_SwapchainFormat);
    if (nvrhiFormat == nvrhi::Format::UNKNOWN)
    {
        SDL_Log("[Init] Unsupported swapchain format: %d", m_RHI.m_SwapchainFormat);
        SDL_assert(false && "Unsupported swapchain format");
        return false;
    }

    for (size_t i = 0; i < GraphicRHI::SwapchainImageCount; ++i)
    {
        nvrhi::TextureDesc textureDesc;
        textureDesc.width = m_RHI.m_SwapchainExtent.width;
        textureDesc.height = m_RHI.m_SwapchainExtent.height;
        textureDesc.format = nvrhiFormat;
        textureDesc.debugName = "SwapchainImage_" + std::to_string(i);
        textureDesc.isRenderTarget = true;
        textureDesc.isUAV = false;
        textureDesc.initialState = nvrhi::ResourceStates::Present;
        textureDesc.keepInitialState = true;

        nvrhi::TextureHandle texture = m_NvrhiDevice->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image,
            nvrhi::Object(m_RHI.m_SwapchainImages[i]),
            textureDesc
        );

        if (!texture)
        {
            SDL_Log("[Init] Failed to create NVRHI texture handle for swap chain image %zu", i);
            SDL_assert(false && "Failed to create NVRHI texture handle");
            return false;
        }

        m_SwapchainTextures[i] = texture;
    }

    SDL_Log("[Init] Created %u NVRHI swap chain texture handles", GraphicRHI::SwapchainImageCount);
    return true;
}

void Renderer::DestroySwapchainTextures()
{
    SDL_Log("[Shutdown] Destroying swap chain texture handles");
    for (size_t i = 0; i < GraphicRHI::SwapchainImageCount; ++i)
    {
        m_SwapchainTextures[i] = nullptr;
    }
}

int main(int argc, char* argv[])
{
    Config::ParseCommandLine(argc, argv);

    Renderer renderer{};
    if (!renderer.Initialize())
        return 1;

    renderer.Run();
    renderer.Shutdown();
    return 0;
}
