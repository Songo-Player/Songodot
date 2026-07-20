#ifdef VULKAN_ENABLED
#include "rendering_context_driver_vulkan_sdl.h"
#include "drivers/vulkan/rendering_context_driver_vulkan.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
//#include <vulkan/vulkan.h>

const char *RenderingContextDriverVulkanSDL::_get_platform_surface_extension() const {
    const char *driver = SDL_GetCurrentVideoDriver();
    if (!driver) {
		print_line("using VK_KHR_display");
        return "VK_KHR_display";
    } else if (!strcmp(driver, "x11")) {
		print_line("using VK_KHR_xlib_surface");
        return "VK_KHR_xlib_surface";
    } else if (!strcmp(driver, "wayland")) {
		print_line("using VK_KHR_wayland_surface");
        return "VK_KHR_wayland_surface";
    }
    return "VK_KHR_display";
}

RenderingContextDriver::SurfaceID RenderingContextDriverVulkanSDL::surface_create(const void *p_platform_data) {
    const WindowPlatformData *wpd = (const WindowPlatformData *)(p_platform_data);
    print_line("surface_create: wpd=" + itos((uintptr_t)wpd));
    if (!wpd || !wpd->window) {
        ERR_FAIL_V_MSG(0, "Invalid WindowPlatformData or null window pointer.");
    }
    print_line("surface_create: wpd->window=" + itos((uintptr_t)(wpd->window)));
    print_line("surface_create: instance=" + itos((uintptr_t)instance_get()));

    const char *driver = SDL_GetCurrentVideoDriver();
    bool is_kmsdrm = driver && !strcmp(driver, "KMSDRM");

    if (is_kmsdrm) {
        // --- Get function pointers ---
        PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetDisplayProps =
            (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)
            vkGetInstanceProcAddr(instance_get(), "vkGetPhysicalDeviceDisplayPropertiesKHR");

        PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModeProps =
            (PFN_vkGetDisplayModePropertiesKHR)
            vkGetInstanceProcAddr(instance_get(), "vkGetDisplayModePropertiesKHR");

        PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurface =
            (PFN_vkCreateDisplayPlaneSurfaceKHR)
            vkGetInstanceProcAddr(instance_get(), "vkCreateDisplayPlaneSurfaceKHR");

        PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetDisplayPlaneProps =
            (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)
            vkGetInstanceProcAddr(instance_get(), "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");

        ERR_FAIL_COND_V_MSG(!vkGetDisplayProps || !vkGetDisplayModeProps ||
            !vkCreateDisplayPlaneSurface || !vkGetDisplayPlaneProps, 0,
            "Missing VK_KHR_display function pointers.");

        // --- Grab display[0] ---
        uint32_t display_count = 0;
        vkGetDisplayProps(physical_device_get(), &display_count, nullptr);
        ERR_FAIL_COND_V_MSG(display_count == 0, 0, "No Vulkan displays found.");

        Vector<VkDisplayPropertiesKHR> displays;
        displays.resize(display_count);
        vkGetDisplayProps(physical_device_get(), &display_count, displays.ptrw());
        VkDisplayKHR display = displays[0].display;

        // --- Find a matching mode, fall back to mode[0] ---
        uint32_t mode_count = 0;
        vkGetDisplayModeProps(physical_device_get(), display, &mode_count, nullptr);
        ERR_FAIL_COND_V_MSG(mode_count == 0, 0, "No display modes available.");

        Vector<VkDisplayModePropertiesKHR> modes;
        modes.resize(mode_count);
        vkGetDisplayModeProps(physical_device_get(), display, &mode_count, modes.ptrw());

        // Log all available modes to help diagnose
        for (uint32_t i = 0; i < mode_count; i++) {
            auto &p = modes[i].parameters;
            print_line("KMSDRM: mode " + itos(i) + ": "
                + itos(p.visibleRegion.width) + "x" + itos(p.visibleRegion.height)
                + " @ " + itos(p.refreshRate) + " mHz");
        }


		// Always use mode[0] as base, prefer higher refresh if same resolution
        VkDisplayModeKHR chosen_mode = modes[0].displayMode;
        VkExtent2D chosen_extent     = modes[0].parameters.visibleRegion;

        for (uint32_t i = 1; i < mode_count; i++) {
            auto &p = modes[i].parameters;
            if (p.visibleRegion.width  == chosen_extent.width &&
                p.visibleRegion.height == chosen_extent.height &&
                p.refreshRate > modes[0].parameters.refreshRate) {
                chosen_mode   = modes[i].displayMode;
                chosen_extent = p.visibleRegion;
                print_line("KMSDRM: preferring higher refresh mode at index " + itos(i));
            }
        }

        //VkDisplayModeKHR chosen_mode = modes[0].displayMode;
        //VkExtent2D chosen_extent     = modes[0].parameters.visibleRegion;

        //for (uint32_t i = 0; i < mode_count; i++) {
        //    auto &p = modes[i].parameters;
        //    if (p.visibleRegion.width  == 1080 &&
        //        p.visibleRegion.height == 1920 &&
        //        p.refreshRate         == 60000) {
        //        chosen_mode   = modes[i].displayMode;
        //        chosen_extent = p.visibleRegion;
        //        print_line("KMSDRM: found 1080x1920@60 mode at index " + itos(i));
        //        break;
        //    }
        //}

        print_line("KMSDRM: using extent "
            + itos(chosen_extent.width) + "x" + itos(chosen_extent.height));

        // --- Grab plane[0] ---
        uint32_t plane_count = 0;
        vkGetDisplayPlaneProps(physical_device_get(), &plane_count, nullptr);
        ERR_FAIL_COND_V_MSG(plane_count == 0, 0, "No display planes found.");

        Vector<VkDisplayPlanePropertiesKHR> planes;
        planes.resize(plane_count);
        vkGetDisplayPlaneProps(physical_device_get(), &plane_count, planes.ptrw());

        // --- Create surface ---
        VkDisplaySurfaceCreateInfoKHR create_info = {};
        create_info.sType           = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        create_info.displayMode     = chosen_mode;
        create_info.planeIndex      = 0;
        create_info.planeStackIndex = planes[0].currentStackIndex;
        //create_info.transform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		create_info.transform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        create_info.alphaMode       = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        create_info.imageExtent     = chosen_extent;

        VkResult res = vkCreateDisplayPlaneSurface(
            instance_get(), &create_info, nullptr, &vk_surface);
        ERR_FAIL_COND_V_MSG(res != VK_SUCCESS, 0,
            "vkCreateDisplayPlaneSurfaceKHR failed: " + itos(res));

    } else {
        // X11 / Wayland
        if (!SDL_Vulkan_CreateSurface(wpd->window, instance_get(), &vk_surface)) {
            ERR_FAIL_V_MSG(0, "SDL_Vulkan_CreateSurface failed: " + String(SDL_GetError()));
        }
    }

    Surface *surface = memnew(Surface);
    surface->vk_surface = vk_surface;
    return SurfaceID(surface);
}

RenderingContextDriverVulkanSDL::RenderingContextDriverVulkanSDL() {
    // Does nothing.
}

RenderingContextDriverVulkanSDL::~RenderingContextDriverVulkanSDL() {
    // Does nothing.
}

#endif // VULKAN_ENABLED