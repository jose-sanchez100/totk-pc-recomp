// Vulkan 1.2 devices. The runtime calls the commands Vulkan 1.3 made core (dynamic rendering, synchronization2, the
// extended dynamic state commands, vkGetDeviceImageMemoryRequirements) through the device-level pointers in vk_cp. On a
// 1.3 device they are the core entry points; on a 1.2 device they come from VK_KHR_dynamic_rendering,
// VK_KHR_synchronization2, VK_EXT_extended_dynamic_state and VK_EXT_extended_dynamic_state2 (and VK_KHR_maintenance4
// when the device has it), whose functions take the same arguments. vk_device.c create_device fills the table
// right after vkCreateDevice. Without the table a 1.2 device would reach the loader's trampolines for names the driver
// never exported.
#pragma once

// A device without VK_KHR_dynamic_rendering (a Kepler GPU's last driver is Vulkan 1.2.175) gets vk_compat_rp.c's
// emulation of the two rendering commands over render passes and framebuffers, and pipelines are made against a
// render pass (vk_compat_pipeline_pass). The emulation needs what a VkRenderingInfo does not carry, the attachments'
// formats and sample count: callers chain a VkCompatRenderingInfo (vk_compat_rendering_next) to the VkRenderingInfo.

// X(name without the "vk" prefix, suffix of the extension that carries the function on a 1.2 device)
#define VK_COMPAT_FNS(X)                                                                                              \
    X(CmdPipelineBarrier2, KHR) X(CmdWriteTimestamp2, KHR)                                                            \
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
    PFN_vkCmdBeginRendering CmdBeginRendering;   // not in VK_COMPAT_FNS: the emulation can stand in for them
    PFN_vkCmdEndRendering CmdEndRendering;
    PFN_vkGetDeviceImageMemoryRequirements GetDeviceImageMemoryRequirements;   // NULL without maintenance4
} VkCompat;
extern VkCompat vk_cp;

// Fills vk_cp for dev. On a 1.3 device (core13) the table holds the loader's own entry points, the ones the runtime
// called before the 1.2 path existed (vk_compat_core in vk_compat_core.c); below 1.3 it holds the extension functions,
// and maintenance4 says that VK_KHR_maintenance4 is enabled. dynamic_rendering: the device has dynamic rendering
// (core, or VK_KHR_dynamic_rendering enabled); without it vk_cp.CmdBeginRendering and CmdEndRendering are the
// emulation. 0 (logged) when a function is missing.
int vk_compat_load(VkDevice dev, int core13, int maintenance4, int dynamic_rendering);

// ---- rendering without dynamic rendering (vk_compat_rp.c)

#define VK_STRUCTURE_TYPE_COMPAT_RENDERING_INFO ((VkStructureType)0x7fffe001)   // private: never reaches a driver
// Framebuffers a caller destroys itself (vk_compat_fb_release) once the GPU is past the command buffer that used them.
typedef struct VkCompatFbList {
    VkFramebuffer fb[4];
    uint32_t n;
} VkCompatFbList;
typedef struct VkCompatRenderingInfo {
    VkStructureType sType;
    const void *pNext;
    VkFormat color[8];            // per colour slot of the VkRenderingInfo, VK_FORMAT_UNDEFINED for a slot without a view
    VkFormat depth_stencil;       // the format of the depth/stencil attachment, VK_FORMAT_UNDEFINED for none
    VkSampleCountFlagBits samples;
    // Nonzero: every view of the pass lives until the GPU is past the submission this names (the fence of a replay
    // submission, vk_exec.c vk_exec_pass_begin), so its framebuffer may serve later passes of that submission with the
    // same render pass, views and size (vk_compat_rp.c, TOTK_VK_FB_CACHE). 0: a framebuffer made for this pass alone.
    uint64_t scope;
    // Non-NULL: the pass's framebuffer goes into this list instead of to vk_defer_framebuffer. For command buffers
    // whose submission signals a fence of its own instead of the timeline vk_defer_framebuffer waits on (the present,
    // gfx_present.c). Not for command buffers the recorder thread (vk_rec.c) records: it begins the pass later.
    VkCompatFbList *own;
} VkCompatRenderingInfo;

extern int vk_compat_emulate_rendering;   // 1: the device lacks dynamic rendering (or TOTK_VK_NO_DYNAMIC_RENDERING is set)

// Points the table's rendering commands at the emulation.
void vk_compat_rp_setup(VkCompat *t);

// The pNext a VkRenderingInfo needs: fills *out and returns it when rendering is emulated, else NULL. depth and stencil
// are the VkPipelineRenderingCreateInfo formats (the depth/stencil attachment has the one that is set).
const void *vk_compat_rendering_next(VkCompatRenderingInfo *out, uint32_t ncolor, const VkFormat *color, VkFormat depth,
                                     VkFormat stencil, uint32_t samples);

// The render pass a graphics pipeline for these attachment formats is created against; every pass the emulation begins
// for the same formats and sample count is compatible with it. VK_NULL_HANDLE (and nothing to set) without emulation.
VkRenderPass vk_compat_pipeline_pass(VkDevice dev, uint32_t ncolor, const VkFormat *color, VkFormat depth, VkFormat stencil,
                                     uint32_t samples);

// A render pass for the Dear ImGui backend, which draws into one colour attachment of format fmt.
VkRenderPass vk_compat_color_pass(VkDevice dev, VkFormat fmt);

// Destroys the render passes (device shutdown).
void vk_compat_rp_shutdown(VkDevice dev);

// Framebuffers the emulation made and the ones it reused for a later pass of a scope since the last call.
void vk_compat_fb_stats(uint64_t *made, uint64_t *reused);

// vk_compat_rp.c leaves each framebuffer it makes to vk_defer_framebuffer (vk_memory.c, declared in vk.h), which
// destroys it once the GPU is past the submission its pass belongs to (VkCompatRenderingInfo.scope), or the one being
// recorded; a pass with VkCompatRenderingInfo.own puts it in that list instead.

// Destroys the framebuffers of a list and empties it. The caller has waited for the command buffer that used them.
void vk_compat_fb_release(VkDevice dev, VkCompatFbList *l);

// vkGetDeviceImageMemoryRequirements, or on a device without it a throwaway image made from the same create info.
void vk_compat_image_requirements(VkDevice dev, const VkDeviceImageMemoryRequirements *info, VkMemoryRequirements2 *out);

// vk_compat_core.c: the loader's functions, in a file that includes nothing of the runtime so that no macro below
// (nor the rc_* ones of vk_rec.h) renames them.
void vk_compat_core(VkCompat *t);

#ifndef VK_COMPAT_CORE
#define vkQueueSubmit2 vk_cp.QueueSubmit2
#define vkGetDeviceImageMemoryRequirements vk_compat_image_requirements
#endif

// The recorder (vk_rec.c) makes the real vkCmd* calls; every other file reaches them through the rc_* macros of vk_rec.h.
#ifdef VK_REC_IMPL
#define vkCmdBeginRendering vk_cp.CmdBeginRendering   // the emulation when the device has no dynamic rendering
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
