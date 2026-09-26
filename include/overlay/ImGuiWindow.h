// SPDX-License-Identifier: AGPL-3.0-only
// Modified by Shinyflvres, 2026-08-23. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md

#pragma once

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include "VulkanRenderer.h"
#include "VrOverlay.h"
#include "UserInterface.h"

class ImGuiWindow
{
public:
    explicit ImGuiWindow();
    auto Initialize(VulkanRenderer*& renderer, VrOverlay*& overlay, const char* name, int width, int height) -> void;

    [[nodiscard]] auto Window() const -> SDL_Window* { return window_; };
    [[nodiscard]] auto WindowData() -> Vulkan_Window* { return &window_data_; };
    [[nodiscard]] auto Shown() const -> bool { return window_shown_; };
    [[nodiscard]] auto Minimized() const -> bool { return window_minimized_; };
    [[nodiscard]] auto Width() const -> int { return width_; };
    [[nodiscard]] auto Height() const -> int { return height_; };
    [[nodiscard]] auto UiScale() const -> float { return ui_scale_; };

    auto Hide() -> void;
    auto Show() -> void;
    auto ShowAndFocus() -> void;
    auto SetMinimizedFromEvent(bool state) -> void;
    auto Draw(bool dashboardVisible, float overlayTopCrop) -> void;
    auto Resize(VulkanRenderer*& renderer, int width, int height) -> void;

    auto Destroy(VulkanRenderer*& renderer) -> void;

private:
    static SDL_HitTestResult HitTest(SDL_Window* window, const SDL_Point* area, void* data);

    SDL_Window* window_;
    Vulkan_Window window_data_;
    int width_;
    int height_;
    float ui_scale_ = 1.0f;
    VkSurfaceFormatKHR render_format_ = {};
    bool window_shown_;
    bool window_minimized_;
    UserInterface m_userInterface_;

public:
    void SetClosing(bool closing) { m_userInterface_.SetClosing(closing); }
};
