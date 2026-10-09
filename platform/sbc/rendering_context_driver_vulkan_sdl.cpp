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


        // Target resolution: the mode the connector is actually running (SDL's
        // desktop mode), which the DisplayServer also sized the window to.
        // Vulkan's mode list order isn't guaranteed to put that mode first.
        uint32_t target_w = 0, target_h = 0;
        SDL_DisplayMode desktop_mode;
        if (SDL_GetDesktopDisplayMode(0, &desktop_mode) == 0) {
            target_w = desktop_mode.w;
            target_h = desktop_mode.h;
        }

        // Some handheld firmwares patch SDL to report portrait panels rotated to
        // landscape, so also accept a mode matching the desktop mode's swapped size.
        int chosen_index = -1;
        for (int pass = 0; pass < 2 && chosen_index < 0; pass++) {
            uint32_t want_w = pass == 0 ? target_w : target_h;
            uint32_t want_h = pass == 0 ? target_h : target_w;
            for (uint32_t i = 0; i < mode_count; i++) {
                auto &p = modes[i].parameters;
                if (p.visibleRegion.width == want_w && p.visibleRegion.height == want_h &&
                    (chosen_index < 0 || p.refreshRate > modes[chosen_index].parameters.refreshRate)) {
                    chosen_index = i;
                }
            }
        }
        if (chosen_index < 0) {
            // No match (or SDL couldn't tell us): fall back to mode[0], preferring
            // the highest refresh rate at that resolution.
            chosen_index = 0;
            for (uint32_t i = 1; i < mode_count; i++) {
                auto &p = modes[i].parameters;
                if (p.visibleRegion.width == modes[0].parameters.visibleRegion.width &&
                    p.visibleRegion.height == modes[0].parameters.visibleRegion.height &&
                    p.refreshRate > modes[chosen_index].parameters.refreshRate) {
                    chosen_index = i;
                }
            }
            WARN_PRINT("KMSDRM: no Vulkan display mode matches the desktop mode "
                + itos(target_w) + "x" + itos(target_h) + ", falling back to mode " + itos(chosen_index) + ".");
        }

        VkDisplayModeKHR chosen_mode = modes[chosen_index].displayMode;
        VkExtent2D chosen_extent     = modes[chosen_index].parameters.visibleRegion;

        print_line("KMSDRM: desktop mode " + itos(target_w) + "x" + itos(target_h)
            + ", using mode " + itos(chosen_index) + ", extent "
            + itos(chosen_extent.width) + "x" + itos(chosen_extent.height));

        // --- Scanout rotation ---
        // Handheld panels are often physically portrait but mounted landscape, so
        // those get rotated 90 degrees clockwise. Natively landscape panels are left
        // as-is. SONGODOT_KMSDRM_ROTATION=0|90|180|270 overrides this (e.g. a panel
        // mounted the other way round needs 270).
        int quarter_turns = chosen_extent.height > chosen_extent.width ? 1 : 0;
        const char *rotation_env = getenv("SONGODOT_KMSDRM_ROTATION");
        if (rotation_env) {
            int degrees = atoi(rotation_env);
            if (degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270) {
                quarter_turns = degrees / 90;
            } else {
                WARN_PRINT("KMSDRM: ignoring invalid SONGODOT_KMSDRM_ROTATION=" + String(rotation_env));
            }
        }

        // Rotate in scanout if the display supports it. Otherwise (e.g. Mesa, which
        // only supports identity) scan out unrotated and have the renderer rotate the
        // image in its final blit to the screen, laying out at the rotated size.
        static const VkSurfaceTransformFlagBitsKHR transforms[4] = {
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
            VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR,
            VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR,
        };
        VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        kmsdrm_blit_rotation = 0;
        kmsdrm_extent = Size2i(chosen_extent.width, chosen_extent.height);
        if (quarter_turns != 0) {
            if (displays[0].supportedTransforms & transforms[quarter_turns]) {
                transform = transforms[quarter_turns];
            } else {
                kmsdrm_blit_rotation = quarter_turns;
                if (quarter_turns & 1) {
                    kmsdrm_extent = Size2i(chosen_extent.height, chosen_extent.width);
                }
            }
        }
        print_line("KMSDRM: rotation " + itos(quarter_turns * 90) + " degrees ("
            + (kmsdrm_blit_rotation ? "in renderer blit" : "in scanout") + "), supported transforms 0x"
            + String::num_int64(displays[0].supportedTransforms, 16)
            + ", layout size " + itos(kmsdrm_extent.width) + "x" + itos(kmsdrm_extent.height));

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
        create_info.transform       = transform;
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