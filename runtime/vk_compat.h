// Vulkan 1.2 devices. The runtime calls the commands Vulkan 1.3 made core (dynamic rendering, synchronization2, the
// extended dynamic state commands, vkGetDeviceImageMemoryRequirements) through the device-level pointers in vk_cp. On a
// 1.3 device they are the core entry points; on a 1.2 device they come from VK_KHR_dynamic_rendering,
// VK_KHR_synchronization2, VK_EXT_extended_dynamic_state and VK_EXT_extended_dynamic_state2 (and VK_KHR_maintenance4
// when the device has it), whose functions take the same arguments. vk_device.c create_device fills the table
// right after vkCreateDevice. Without the table a 1.2 device would reach the loader's trampolines for names the driver
// never exported.
#pragma once

// X(name without the "vk" prefix, suffix of the extension that carries the function on a 1.2 device)
#define VK_COMPAT_FNS(X)                                                                                              \
    X(CmdBeginRendering, KHR) X(CmdEndRendering, KHR) X(CmdPipelineBarrier2, KHR) X(CmdWriteTimestamp2, KHR)          \
    X(QueueSubmit2, KHR)                                                                                              \
    X(CmdSetCullMode, EXT) X(CmdSetFrontFace, EXT) X(CmdSetPrimitiveTopology, EXT) X(CmdSetViewportWithCount, EXT)    \
    X(CmdSetScissorWithCount, EXT) X(CmdBindVertexBuffers2, EXT) X(CmdSetDepthTestEnable, EXT)                        \
    X(CmdSetDepthWriteEnable, EXT) X(CmdSetDepthCompareOp, EXT) X(CmdSetStencilTestEnable, EXT)                       \
    X(CmdSetStencilOp, EXT) X(CmdSetRasterizerDiscardEnable, EXT) X(CmdSetDepthBiasEnable, EXT)                       \
    X(CmdSetPrimitiveRestartEnable, EXT)

typedef struct VkCompat {
#define VK_COMPAT_FIELD(name, sfx) PFN_vk##name name;
    VK_COMPAT_FNS(VK_COMPAT_FIELD)
#undef VK_COMPAT_FIELD
    PFN_vkGetDeviceImageMemoryRequirements GetDeviceImageMemoryRequirements;   // NULL without maintenance4
} VkCompat;
extern VkCompat vk_cp;

// Fills vk_cp for dev: core13 selects the core names, otherwise the extension ones; maintenance4 says that
// VK_KHR_maintenance4 is enabled on a 1.2 device. 0 (logged) when a function is missing.
int vk_compat_load(VkDevice dev, int core13, int maintenance4);

// vkGetDeviceImageMemoryRequirements, or on a device without it a throwaway image made from the same create info.
void vk_compat_image_requirements(VkDevice dev, const VkDeviceImageMemoryRequirements *info, VkMemoryRequirements2 *out);

#define vkQueueSubmit2 vk_cp.QueueSubmit2
#define vkGetDeviceImageMemoryRequirements vk_compat_image_requirements

// The recorder (vk_rec.c) makes the real vkCmd* calls; every other file reaches them through the rc_* macros of vk_rec.h.
#ifdef VK_REC_IMPL
#define vkCmdBeginRendering vk_cp.CmdBeginRendering
#define vkCmdEndRendering vk_cp.CmdEndRendering
#define vkCmdPipelineBarrier2 vk_cp.CmdPipelineBarrier2
#define vkCmdWriteTimestamp2 vk_cp.CmdWriteTimestamp2
#define vkCmdSetCullMode vk_cp.CmdSetCullMode
#define vkCmdSetFrontFace vk_cp.CmdSetFrontFace
#define vkCmdSetPrimitiveTopology vk_cp.CmdSetPrimitiveTopology
#define vkCmdSetViewportWithCount vk_cp.CmdSetViewportWithCount
#define vkCmdSetScissorWithCount vk_cp.CmdSetScissorWithCount
#define vkCmdBindVertexBuffers2 vk_cp.CmdBindVertexBuffers2
#define vkCmdSetDepthTestEnable vk_cp.CmdSetDepthTestEnable
#define vkCmdSetDepthWriteEnable vk_cp.CmdSetDepthWriteEnable
#define vkCmdSetDepthCompareOp vk_cp.CmdSetDepthCompareOp
#define vkCmdSetStencilTestEnable vk_cp.CmdSetStencilTestEnable
#define vkCmdSetStencilOp vk_cp.CmdSetStencilOp
#define vkCmdSetRasterizerDiscardEnable vk_cp.CmdSetRasterizerDiscardEnable
#define vkCmdSetDepthBiasEnable vk_cp.CmdSetDepthBiasEnable
#define vkCmdSetPrimitiveRestartEnable vk_cp.CmdSetPrimitiveRestartEnable
#endif
