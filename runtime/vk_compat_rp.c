// Rendering on a device without VK_KHR_dynamic_rendering (vk_compat.h): vkCmdBeginRendering and vkCmdEndRendering over
// a render pass with one subpass and a framebuffer made for the call, and the render pass a graphics pipeline is
// created against. The runtime's passes keep every target in one layout for the whole pass (GENERAL, or the one the
// caller names) and barrier before and after it, so the render pass leaves initialLayout = finalLayout = the layout
// the subpass uses. Its one dependency is the subpass on itself, for the barriers of feedback loops (make_pass).
//
// Render passes are cached by the formats, the sample count and the load/store operations and layouts, and shared by
// every pass and pipeline with that key. Two render passes with the same formats and sample count are compatible
// whatever their operations and layouts, so a pipeline made against one runs in the passes made from the others.
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"
#include "vk.h"

int vk_compat_emulate_rendering;

typedef struct RpKey {
    VkFormat color[8], ds;
    uint32_t ncolor, samples;
    VkImageLayout color_layout[8], ds_layout;
    uint8_t color_load[8], color_store[8];
    uint8_t depth_load, depth_store, stencil_load, stencil_store;
} RpKey;

typedef struct RpEnt { struct RpEnt *next; uint64_t hash; VkRenderPass pass; RpKey key; } RpEnt;

#define RP_BUCKETS 256
static RpEnt *rp_tab[RP_BUCKETS];
static PlatMutex rp_lock = PLAT_MUTEX_INIT;

static uint64_t key_hash(const RpKey *k) {
    uint64_t h = 1469598103934665603ull;   // FNV-1a over the bytes; the key is zeroed before it is filled
    for (size_t i = 0; i < sizeof *k; i++) h = (h ^ ((const uint8_t *)k)[i]) * 1099511628211ull;
    return h;
}

static VkRenderPass make_pass(VkDevice dev, const RpKey *k) {
    VkAttachmentDescription ad[9];
    VkAttachmentReference cr[8], dr = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
    uint32_t n = 0;
    for (uint32_t i = 0; i < k->ncolor; i++) {
        cr[i] = (VkAttachmentReference){VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
        if (k->color[i] == VK_FORMAT_UNDEFINED) continue;
        ad[n] = (VkAttachmentDescription){.format = k->color[i], .samples = (VkSampleCountFlagBits)k->samples,
                                          .loadOp = k->color_load[i], .storeOp = k->color_store[i],
                                          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          .initialLayout = k->color_layout[i], .finalLayout = k->color_layout[i]};
        cr[i] = (VkAttachmentReference){n++, k->color_layout[i]};
    }
    if (k->ds != VK_FORMAT_UNDEFINED) {
        ad[n] = (VkAttachmentDescription){.format = k->ds, .samples = (VkSampleCountFlagBits)k->samples,
                                          .loadOp = k->depth_load, .storeOp = k->depth_store,
                                          .stencilLoadOp = k->stencil_load, .stencilStoreOp = k->stencil_store,
                                          .initialLayout = k->ds_layout, .finalLayout = k->ds_layout};
        dr = (VkAttachmentReference){n++, k->ds_layout};
    }
    VkSubpassDescription sp = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = k->ncolor,
                               .pColorAttachments = cr, .pDepthStencilAttachment = dr.attachment == VK_ATTACHMENT_UNUSED ? NULL : &dr};
    // The subpass depends on itself, so a draw that samples a target an earlier draw of the pass wrote can follow a
    // by-region barrier inside the pass (vk.h VK_FEEDBACK_SUBPASS, vk_exec_draw.c feedback_barrier) instead of a
    // restart. Every render pass has it: dependencies count for compatibility, and pipelines are made against these.
    VkSubpassDependency self = {.srcSubpass = 0, .dstSubpass = 0,
                                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT};
    VkRenderPassCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = n, .pAttachments = ad,
                                 .subpassCount = 1, .pSubpasses = &sp, .dependencyCount = 1, .pDependencies = &self};
    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(dev, &ci, NULL, &rp) != VK_SUCCESS) rt_fatal("vk: vkCreateRenderPass failed");
    return rp;
}

static VkRenderPass pass_for(VkDevice dev, const RpKey *k) {
    uint64_t h = key_hash(k);
    plat_mutex_lock(&rp_lock);
    RpEnt *e = rp_tab[h % RP_BUCKETS];
    while (e && (e->hash != h || memcmp(&e->key, k, sizeof *k))) e = e->next;
    if (!e) {
        e = malloc(sizeof *e);
        if (!e) rt_fatal("vk: out of memory");
        *e = (RpEnt){.hash = h, .pass = make_pass(dev, k), .key = *k};
        e->next = rp_tab[h % RP_BUCKETS];
        rp_tab[h % RP_BUCKETS] = e;
    }
    VkRenderPass rp = e->pass;
    plat_mutex_unlock(&rp_lock);
    return rp;
}

// The key of a pipeline's render pass: the operations are the ones every pass can be made compatible with.
static void pipeline_key(RpKey *k, uint32_t ncolor, const VkFormat *color, VkFormat depth, VkFormat stencil, uint32_t samples) {
    memset(k, 0, sizeof *k);
    k->ncolor = ncolor;
    k->samples = samples ? samples : 1;
    for (uint32_t i = 0; i < ncolor; i++) {
        k->color[i] = color[i];
        k->color_layout[i] = VK_IMAGE_LAYOUT_GENERAL;
        k->color_load[i] = VK_ATTACHMENT_LOAD_OP_LOAD, k->color_store[i] = VK_ATTACHMENT_STORE_OP_STORE;
    }
    k->ds = depth != VK_FORMAT_UNDEFINED ? depth : stencil;   // one attachment holds both planes
    k->ds_layout = VK_IMAGE_LAYOUT_GENERAL;
    k->depth_load = k->stencil_load = VK_ATTACHMENT_LOAD_OP_LOAD;
    k->depth_store = k->stencil_store = VK_ATTACHMENT_STORE_OP_STORE;
}

const void *vk_compat_rendering_next(VkCompatRenderingInfo *out, uint32_t ncolor, const VkFormat *color, VkFormat depth,
                                     VkFormat stencil, uint32_t samples) {
    if (!vk_compat_emulate_rendering) return NULL;
    *out = (VkCompatRenderingInfo){.sType = VK_STRUCTURE_TYPE_COMPAT_RENDERING_INFO,
                                   .depth_stencil = depth != VK_FORMAT_UNDEFINED ? depth : stencil,
                                   .samples = (VkSampleCountFlagBits)(samples ? samples : 1)};
    for (uint32_t i = 0; i < 8; i++) out->color[i] = i < ncolor ? color[i] : VK_FORMAT_UNDEFINED;
    return out;
}

VkRenderPass vk_compat_pipeline_pass(VkDevice dev, uint32_t ncolor, const VkFormat *color, VkFormat depth, VkFormat stencil,
                                     uint32_t samples) {
    if (!vk_compat_emulate_rendering) return VK_NULL_HANDLE;
    RpKey k;
    pipeline_key(&k, ncolor, color, depth, stencil, samples);
    return pass_for(dev, &k);
}

VkRenderPass vk_compat_color_pass(VkDevice dev, VkFormat fmt) {
    RpKey k;
    pipeline_key(&k, 1, &fmt, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, 1);
    k.color_layout[0] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return pass_for(dev, &k);
}

void vk_compat_rp_shutdown(VkDevice dev) {
    plat_mutex_lock(&rp_lock);
    for (int i = 0; i < RP_BUCKETS; i++) {
        for (RpEnt *e = rp_tab[i], *next; e; e = next) {
            next = e->next;
            vkDestroyRenderPass(dev, e->pass, NULL);
            free(e);
        }
        rp_tab[i] = NULL;
    }
    plat_mutex_unlock(&rp_lock);
}

// ---- the two commands

static const VkCompatRenderingInfo *compat_info(const VkRenderingInfo *ri) {
    for (const VkBaseInStructure *s = ri->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_COMPAT_RENDERING_INFO) return (const VkCompatRenderingInfo *)s;
    return NULL;
}

static void rp_begin_rendering(VkCommandBuffer cb, const VkRenderingInfo *ri) {
    const VkCompatRenderingInfo *ci = compat_info(ri);
    if (!ci) rt_fatal("vk: vkCmdBeginRendering without a VkCompatRenderingInfo on a device without dynamic rendering");
    if (ri->colorAttachmentCount > 8) rt_fatal("vk: a rendering pass with %u colour attachments", ri->colorAttachmentCount);
    RpKey k;
    memset(&k, 0, sizeof k);
    k.ncolor = ri->colorAttachmentCount;
    k.samples = ci->samples ? (uint32_t)ci->samples : 1;
    VkImageView views[9];
    VkClearValue clear[9];
    uint32_t n = 0;
    memset(clear, 0, sizeof clear);
    for (uint32_t i = 0; i < k.ncolor; i++) {
        const VkRenderingAttachmentInfo *a = &ri->pColorAttachments[i];
        if (!a->imageView) continue;   // a slot without a view: unused, as its pipelines' format is VK_FORMAT_UNDEFINED
        k.color[i] = ci->color[i];
        k.color_layout[i] = a->imageLayout;
        k.color_load[i] = (uint8_t)a->loadOp, k.color_store[i] = (uint8_t)a->storeOp;
        clear[n] = a->clearValue;
        views[n++] = a->imageView;
    }
    const VkRenderingAttachmentInfo *da = ri->pDepthAttachment, *sa = ri->pStencilAttachment;
    k.depth_load = k.stencil_load = VK_ATTACHMENT_LOAD_OP_LOAD;    // a plane the caller does not attach keeps its contents
    k.depth_store = k.stencil_store = VK_ATTACHMENT_STORE_OP_STORE;
    if ((da && da->imageView) || (sa && sa->imageView)) {
        k.ds = ci->depth_stencil;
        k.ds_layout = da && da->imageView ? da->imageLayout : sa->imageLayout;
        if (da && da->imageView) k.depth_load = (uint8_t)da->loadOp, k.depth_store = (uint8_t)da->storeOp;
        if (sa && sa->imageView) k.stencil_load = (uint8_t)sa->loadOp, k.stencil_store = (uint8_t)sa->storeOp;
        if (da && da->imageView) clear[n].depthStencil.depth = da->clearValue.depthStencil.depth;
        if (sa && sa->imageView) clear[n].depthStencil.stencil = sa->clearValue.depthStencil.stencil;
        views[n++] = da && da->imageView ? da->imageView : sa->imageView;
    }
    VkDevice dev = vk_ctx()->device;
    VkRenderPass rp = pass_for(dev, &k);
    VkFramebufferCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = rp, .attachmentCount = n,
                                  .pAttachments = views, .width = ri->renderArea.offset.x + ri->renderArea.extent.width,
                                  .height = ri->renderArea.offset.y + ri->renderArea.extent.height,
                                  .layers = ri->layerCount ? ri->layerCount : 1};
    VkFramebuffer fb;
    if (vkCreateFramebuffer(dev, &fi, NULL, &fb) != VK_SUCCESS) rt_fatal("vk: vkCreateFramebuffer failed");
    vk_defer_framebuffer(fb);
    VkRenderPassBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp, .framebuffer = fb,
                                .renderArea = ri->renderArea, .clearValueCount = n, .pClearValues = clear};
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

static void rp_end_rendering(VkCommandBuffer cb) { vkCmdEndRenderPass(cb); }

void vk_compat_rp_setup(VkCompat *t) {
    t->CmdBeginRendering = rp_begin_rendering;
    t->CmdEndRendering = rp_end_rendering;
}
