// The Vulkan 1.3 entry points of the loader, for vk_cp on a 1.3 device (vk_compat.h). A file of its own: vk.h and
// vk_rec.h rename these functions, and here they must be the loader's.
#define VK_COMPAT_CORE
#include <vulkan/vulkan.h>

#include "vk_compat.h"

void vk_compat_core(VkCompat *t) {
    t->CmdBeginRendering = vkCmdBeginRendering;
    t->CmdEndRendering = vkCmdEndRendering;
    t->CmdPipelineBarrier2 = vkCmdPipelineBarrier2;
    t->CmdWriteTimestamp2 = vkCmdWriteTimestamp2;
    t->QueueSubmit2 = vkQueueSubmit2;
    t->CmdSetCullMode = vkCmdSetCullMode;
    t->CmdSetFrontFace = vkCmdSetFrontFace;
    t->CmdSetPrimitiveTopology = vkCmdSetPrimitiveTopology;
    t->CmdSetViewportWithCount = vkCmdSetViewportWithCount;
    t->CmdSetScissorWithCount = vkCmdSetScissorWithCount;
    t->CmdBindVertexBuffers2 = vkCmdBindVertexBuffers2;
    t->CmdSetDepthTestEnable = vkCmdSetDepthTestEnable;
    t->CmdSetDepthWriteEnable = vkCmdSetDepthWriteEnable;
    t->CmdSetDepthCompareOp = vkCmdSetDepthCompareOp;
    t->CmdSetStencilTestEnable = vkCmdSetStencilTestEnable;
    t->CmdSetStencilOp = vkCmdSetStencilOp;
    t->CmdSetRasterizerDiscardEnable = vkCmdSetRasterizerDiscardEnable;
    t->CmdSetDepthBiasEnable = vkCmdSetDepthBiasEnable;
    t->CmdSetPrimitiveRestartEnable = vkCmdSetPrimitiveRestartEnable;
    t->GetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;
}
