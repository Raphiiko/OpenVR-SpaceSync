// SPDX-License-Identifier: AGPL-3.0-only
// Modified by Shinyflvres, 2026-08-23. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md

#ifdef _WIN32
#include <Windows.h>
#endif

#include "ImGuiWindow.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include "imgui_impl_openvr.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <math.h>

#include "Theme.h"
#include "UserInterface.h"

#define IMGUI_NORMALIZED_RGBA(r, g, b, a) ImVec4(((r) / 255.0f), ((g) / 255.0f), ((b) / 255.0f), ((a) / 255.0f))

ImGuiWindow::ImGuiWindow()
{
    window_ = nullptr;
    window_data_ = {};
    width_ = 0;
    height_ = 0;
    window_shown_ = false;
    window_minimized_ = false;
}

auto ImGuiWindow::Initialize(VulkanRenderer*& renderer, VrOverlay*& overlay, const char* name, int width, int height) -> void
{
    // width/height are design px, scaled by the display's DPI scale.
    auto sdl_window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE;
    window_ = SDL_CreateWindow(name, width, height, sdl_window_flags);
    if (window_ == nullptr) {
#ifdef _WIN32
        MessageBoxA(NULL, SDL_GetError(), "SpaceSync", MB_OK);
#else
        printf("SDL_CreateWindow(): %s\n", SDL_GetError());
#endif
        return;
    }

    // Windows DPI scale. The user's UI Scale is applied inside the window.
    float scale = SDL_GetWindowDisplayScale(window_);
    if (!(scale >= 1.0f && scale <= 2.0f))
        scale = 1.0f;
    ui_scale_ = scale;
    width_ = (int)(width * scale + 0.5f);
    height_ = (int)(height * scale + 0.5f);
    SDL_SetWindowSize(window_, width_, height_);
    SDL_SetWindowHitTest(window_, &ImGuiWindow::HitTest, this);

    VkSurfaceKHR surface = {};
    if (SDL_Vulkan_CreateSurface(window_, renderer->Instance(), renderer->Allocator(), &surface) == 0) {
#ifdef _WIN32
        MessageBoxA(NULL, SDL_GetError(), "SpaceSync", MB_OK);
#else
        printf("SDL_Vulkan_CreateSurface(): %s\n", SDL_GetError());
#endif
        return;
    }

    Vulkan_Window* window = &window_data_;
    renderer->SetupWindow(window, surface, width_, height_);

    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_CaptureMouse(false);

    window_shown_ = false;

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(width_), static_cast<float>(height_));

    ui::Init(ui_scale_, false);

    VkSurfaceFormatKHR render_format =
    {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    };

    VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info =
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &render_format.format,
        .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = VK_API_VERSION_1_3,
        .Instance = renderer->Instance(),
        .PhysicalDevice = renderer->PhysicalDevice(),
        .Device = renderer->Device(),
        .QueueFamily = renderer->QueueFamily(),
        .Queue = renderer->Queue(),
        .DescriptorPool = renderer->DescriptorPool(),
        .MinImageCount = renderer->MinimumConcurrentImageCount(),
        .ImageCount = window->image_count,
        .PipelineCache = renderer->PipelineCache(),
        .PipelineInfoMain = {
            .RenderPass = VK_NULL_HANDLE,
            .Subpass = 0,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            .PipelineRenderingCreateInfo = pipeline_rendering_create_info,
        },
        .UseDynamicRendering = true,
        .Allocator = renderer->Allocator(),
        .CheckVkResultFn = nullptr,
    };

    ImGui_ImplSDL3_InitForVulkan(window_);
    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplOpenVR_InitInfo openvr_init_info =
    {
        .handle = overlay->Handle(),
        .width = width_,
        .height = height_
    };

    ImGui_ImplOpenVR_Init(&openvr_init_info);

    render_format_ = render_format;
    renderer->SetupRenderTarget(width_, height_, render_format_);

    SDL_SetWindowMinimumSize(window_, 800, 560);

    m_userInterface_ = {};
}

auto ImGuiWindow::Resize(VulkanRenderer*& renderer, int width, int height) -> void
{
    if (width < 1 || height < 1 || (width == width_ && height == height_))
        return;

    width_ = width;
    height_ = height;

    renderer->DestroyRenderTarget();
    renderer->SetupRenderTarget(width_, height_, render_format_);
    renderer->RequestSwapchainRebuild();
}

auto ImGuiWindow::Show() -> void
{
    SDL_ShowWindow(window_);
    SDL_RestoreWindow(window_);

    window_shown_ = true;
}

auto ImGuiWindow::ShowAndFocus() -> void
{
    SDL_ShowWindow(window_);
    SDL_RestoreWindow(window_);
    SDL_RaiseWindow(window_);

    window_shown_ = true;
}

auto ImGuiWindow::SetMinimizedFromEvent(bool state) -> void
{
    window_minimized_ = state;
}

auto ImGuiWindow::Hide() -> void
{
    SDL_HideWindow(window_);

    window_shown_ = false;
}

auto ImGuiWindow::Draw(bool dashboardVisible, float overlayTopCrop) -> void
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width_), static_cast<float>(height_));

    ImGui_ImplOpenVR_SetTopCrop(overlayTopCrop);
    ImGui_ImplOpenVR_NewFrame(dashboardVisible);

    ImGui::NewFrame();

    m_userInterface_.SetOverlayInput(dashboardVisible);
    UserInterface::WindowAction action = m_userInterface_.Render(false);

    ImGui::Render();

    switch (action)
    {
    case UserInterface::WindowAction::Minimize:
        SDL_MinimizeWindow(window_);
        break;
    case UserInterface::WindowAction::Close:
    {
        SDL_Event quitEvent = {};
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
        break;
    }
    default:
        break;
    }
}

SDL_HitTestResult ImGuiWindow::HitTest(SDL_Window* window, const SDL_Point* area, void* data)
{
    auto* self = static_cast<ImGuiWindow*>(data);
    if (!self || !area)
        return SDL_HITTEST_NORMAL;

    const float s = ui::S();

    const int edge = (int)(6.0f * s + 0.5f);
    const bool left = area->x < edge;
    const bool right = area->x >= self->width_ - edge;
    const bool top = area->y < edge;
    const bool bottom = area->y >= self->height_ - edge;

    if (top && left)     return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right)    return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (left)            return SDL_HITTEST_RESIZE_LEFT;
    if (right)           return SDL_HITTEST_RESIZE_RIGHT;
    if (top)             return SDL_HITTEST_RESIZE_TOP;
    if (bottom)          return SDL_HITTEST_RESIZE_BOTTOM;

    // Title bar drags the window, except over the buttons.
    const float titleH = UserInterface::TitleBarHeight * s;
    const float buttonsW = UserInterface::TitleBarButtonWidth * UserInterface::TitleBarButtonCount * s + 4.0f * s;
    const bool overLangSwitch = area->x >= UserInterface::sLangZoneMinX && area->x <= UserInterface::sLangZoneMaxX;
    if (area->y >= 0 && area->y < titleH && area->x < self->width_ - buttonsW && !overLangSwitch)
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

auto ImGuiWindow::Destroy(VulkanRenderer*& renderer) -> void
{
    ImGui_ImplOpenVR_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    SDL_DestroyWindow(window_);
}
