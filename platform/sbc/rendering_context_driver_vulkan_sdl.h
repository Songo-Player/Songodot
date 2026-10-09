#ifndef RENDERING_CONTEXT_DRIVER_VULKAN_SDL_H
#define RENDERING_CONTEXT_DRIVER_VULKAN_SDL_H
#ifdef VULKAN_ENABLED

#include "drivers/vulkan/rendering_context_driver_vulkan.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

class RenderingContextDriverVulkanSDL : public RenderingContextDriverVulkan {
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    // Size Godot should lay out and render at on KMSDRM (the swapchain size, swapped
    // when kmsdrm_blit_rotation is 90/270 degrees), or (0, 0) if not on KMSDRM.
    Size2i kmsdrm_extent;
    // Clockwise quarter turns the renderer must apply when blitting to the screen,
    // because the display can't rotate scanout itself.
    int kmsdrm_blit_rotation = 0;

private:
    virtual const char *_get_platform_surface_extension() const override final;

protected:
    SurfaceID surface_create(const void *p_platform_data) override final;

public:
    struct WindowPlatformData {
        SDL_Window *window;
    };

    Size2i get_kmsdrm_extent() const { return kmsdrm_extent; }
    int get_kmsdrm_blit_rotation() const { return kmsdrm_blit_rotation; }

    RenderingContextDriverVulkanSDL();
    ~RenderingContextDriverVulkanSDL();
};

#endif // VULKAN_ENABLED
#endif // RENDERING_CONTEXT_DRIVER_VULKAN_SDL_H