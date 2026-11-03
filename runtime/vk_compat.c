// Vulkan 1.2 devices: the function table and the image-size query; see vk_compat.h.
#include <stdio.h>

#include "host.h"
#include "vk.h"

VkCompat vk_cp;

int vk_compat_load(VkDevice dev, int core13, int maintenance4) {
    if (core13) {   // the runtime's calls on a 1.3 device are the loader's, as they were before the 1.2 path
        vk_compat_core(&vk_cp);
        return 1;
    }
    int missing = 0;
#define VK_COMPAT_LOAD(name, sfx)                                                                                     \
    do {                                                                                                              \
        const char *n = "vk" #name #sfx;                                                                              \
        vk_cp.name = (PFN_vk##name)vkGetDeviceProcAddr(dev, n);                                                       \
        if (!vk_cp.name) { rt_log("vk: %s is missing", n); missing++; }                                               \
    } while (0);
    VK_COMPAT_FNS(VK_COMPAT_LOAD)
#undef VK_COMPAT_LOAD
    vk_cp.GetDeviceImageMemoryRequirements =
        maintenance4 ? (PFN_vkGetDeviceImageMemoryRequirements)vkGetDeviceProcAddr(dev, "vkGetDeviceImageMemoryRequirementsKHR")
                     : NULL;
    return missing == 0;
}

void vk_compat_image_requirements(VkDevice dev, const VkDeviceImageMemoryRequirements *info, VkMemoryRequirements2 *out) {
    if (vk_cp.GetDeviceImageMemoryRequirements) {
        vk_cp.GetDeviceImageMemoryRequirements(dev, info, out);
        return;
    }
    // No maintenance4: a throwaway image gives the same answer (callers pass the image's create info and read the size,
    // alignment and memory types). A create that fails leaves *out zero, as the real query does for a layout it cannot make.
    VkImage img = VK_NULL_HANDLE;
    if (vkCreateImage(dev, info->pCreateInfo, NULL, &img) != VK_SUCCESS) return;
    VkImageMemoryRequirementsInfo2 ri = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, .image = img};
    vkGetImageMemoryRequirements2(dev, &ri, out);
    vkDestroyImage(dev, img, NULL);
}
