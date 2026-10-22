// vk_backend.cpp - the FramePlan on Vulkan 1.0 (see backend.h).
//  - every pass is its own render pass instance; each one clears or overwrites its whole image, so no
//    image is ever loaded (loadOp CLEAR / DONT_CARE: cheap on the tiled GPUs of Android devices)
//  - the parameters are push constants; the instance data goes into a persistently mapped buffer per
//    frame in flight (two), no staging copies
//  - descriptor sets are made once per combination of images and kept until the images change
//  - the screen: one full-screen pass (curve, tone and bloom in one shader) and the overlay on top - or, with
//    the overlay's scratches, the screen into an image and the sheet pass (sheet.frag) onto the display
//  - pre-rotation: the swapchain in the display's own orientation, the picture turned into it
#include "backend.h"
#include <android/log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <map>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VectrexEMU", __VA_ARGS__)

namespace vx {
namespace {

const int FRAMES = 2;
const int NBIND = 9;                                    // (sampled images per pass, at bindings 1-9)

struct Img { VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkFramebuffer fb = VK_NULL_HANDLE; int w = 0, h = 0; };
struct Buf { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; void *map = nullptr; size_t size = 0; };

class Vk : public Backend {
public:
    ~Vk() override { destroy(); }
    const char *name() const override { return "Vulkan"; }
    bool float_images() const override { return off_fmt == VK_FORMAT_R16G16B16A16_SFLOAT; }
    bool can_sheet() const override { return sheet_ok; }

    bool init(ANativeWindow *win, std::string &err) override
    {
        window = win;
        VkApplicationInfo ai{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        ai.pApplicationName = "Vectrex_EMU"; ai.apiVersion = VK_API_VERSION_1_0;
        const char *iext[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
        VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ici.pApplicationInfo = &ai; ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = iext;
        if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) { err = "no Vulkan instance"; instance = VK_NULL_HANDLE; return false; }
        VkAndroidSurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
        sci.window = win;
        if (vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) { err = "no Vulkan surface"; return false; }
        uint32_t n = 0; vkEnumeratePhysicalDevices(instance, &n, nullptr);
        if (!n) { err = "no Vulkan device"; return false; }
        std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(instance, &n, pds.data());
        for (auto d : pds) {
            uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qp(qn); vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qp.data());
            for (uint32_t i = 0; i < qn; i++) {
                VkBool32 pres = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &pres);
                if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) { pd = d; qfam = i; break; }
            }
            if (pd) break;
        }
        if (!pd) { err = "no Vulkan queue that can draw and present"; return false; }
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
        LOGI("Vulkan: %s, API %u.%u.%u, driver %u", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
             VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion), props.driverVersion);
        if (props.limits.maxPushConstantsSize < 128) { err = "too few push constants"; return false; }
        vkGetPhysicalDeviceMemoryProperties(pd, &memp);
        float prio = 1.f;
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = qfam; qci.queueCount = 1; qci.pQueuePriorities = &prio;
        const char *dext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dext;
        if (vkCreateDevice(pd, &dci, nullptr, &dev) != VK_SUCCESS) { err = "no Vulkan device"; dev = VK_NULL_HANDLE; return false; }
        vkGetDeviceQueue(dev, qfam, 0, &queue);
        // the phosphor images: 16-bit float (mandatory in Vulkan with blending; checked all the same); the
        // glint's harmonics: 32-bit float where it can be drawn into (sums of large and small terms)
        VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R16G16B16A16_SFLOAT, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        off_fmt = (fp.optimalTilingFeatures & need) == need ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R32G32B32A32_SFLOAT, &fp);
        h_fmt = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ? VK_FORMAT_R32G32B32A32_SFLOAT : off_fmt;
        VkCommandPoolCreateInfo cpi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = qfam;
        vkCreateCommandPool(dev, &cpi, nullptr, &pool);
        VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = FRAMES;
        vkAllocateCommandBuffers(dev, &cai, cmds);
        for (int i = 0; i < FRAMES; i++) {
            VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(dev, &fci, nullptr, &fences[i]);
            VkSemaphoreCreateInfo s{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCreateSemaphore(dev, &s, nullptr, &acquired[i]);
        }
        if (!make_swapchain(err)) return false;
        if (!make_pipelines(err)) return false;
        return true;
    }

    void size(int &w, int &h) override { w = lw; h = lh; }      // (upright: the window as the user sees it)

    void set_art(int w, int h, const std::vector<uint8_t> &rgba) override
    {
        vkDeviceWaitIdle(dev);
        reset_sets();
        free_img(art);
        if (w <= 0) return;
        make_img(art, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_NULL_HANDLE);
        Buf st; make_buf(st, rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        memcpy(st.map, rgba.data(), rgba.size());
        VkCommandBuffer c = once_begin();
        barrier(c, art.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy r{}; r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.imageSubresource.layerCount = 1;
        r.imageExtent = { (uint32_t) w, (uint32_t) h, 1 };
        vkCmdCopyBufferToImage(c, st.buf, art.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        barrier(c, art.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        once_end(c);
        free_buf(st);
    }

    bool make_scratches(const std::vector<ScrDraw> &draws) override
    {
        if (!sheet_ok) return false;
        vkDeviceWaitIdle(dev);
        reset_sets();
        const VkImageUsageFlags use = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        for (Img &m : scr) { free_img(m); make_img(m, SCR_TW, SCR_TH, off_fmt, use, rp_off_clear); }
        VkCommandBuffer c = once_begin();
        for (int t = 0; t < 3; t++) {                   // (each texture: cleared, then its scratches added up)
            begin(c, rp_off_clear, scr[t].fb, SCR_TW, SCR_TH);
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_scr);
            for (auto &d : draws) {
                if (d.tex != t) continue;
                float pc[24] = { 0, 0, 0, 0 }; memcpy(pc + 4, d.pc, sizeof d.pc);
                push(c, pc, sizeof pc);
                vkCmdDraw(c, SCR_VERTS, 1, 0, 0);
            }
            vkCmdEndRenderPass(c);
        }
        once_end(c);
        return true;
    }

    bool render(const FramePlan &p) override
    {
        if (!swapchain) { std::string e; if (!make_swapchain(e)) return false; }
        int fi = frame++ % FRAMES;
        vkWaitForFences(dev, 1, &fences[fi], VK_TRUE, UINT64_MAX);
        if (p.tw != tw || p.th != th) { vkDeviceWaitIdle(dev); targets(p.tw, p.th, p.qw, p.qh); }
        if (p.sheet && (S.w != p.screen_w || S.h != p.screen_h)) { vkDeviceWaitIdle(dev); sheet_targets(p); }
        uint32_t si = 0;
        VkResult r = vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX, acquired[fi], VK_NULL_HANDLE, &si);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { std::string e; vkDeviceWaitIdle(dev); free_swapchain(); return make_swapchain(e); }
        if (r == VK_ERROR_SURFACE_LOST_KHR) return false;
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return true;
        vkResetFences(dev, 1, &fences[fi]);
        // the instance data, straight into this frame's buffer
        Buf &ib = inst[fi];
        size_t need = std::max<size_t>(1, p.inst.size()) * sizeof(Instance);
        if (ib.size < need) { free_buf(ib); make_buf(ib, std::max(need * 2, (size_t) 1 << 20), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); }
        if (!p.inst.empty()) memcpy(ib.map, p.inst.data(), p.inst.size() * sizeof(Instance));

        VkCommandBuffer c = cmds[fi];
        vkResetCommandBuffer(c, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(c, &bi);
        // 1. the fresh light (phosphor) / the crisp lines (into D)
        Img &cap_to = p.phosphor ? F : D;
        begin(c, rp_off_clear, cap_to.fb, cap_to.w, cap_to.h);
        if (!p.batches.empty()) {
            VkDeviceSize zero = 0; vkCmdBindVertexBuffers(c, 0, 1, &ib.buf, &zero);
            VkPipeline bound = VK_NULL_HANDLE;
            for (auto &b : p.batches) {
                if (!b.count) continue;
                bool mx = p.even || b.max;
                VkPipeline cp = p.phosphor ? (mx ? pipe_cap_timed_max : pipe_cap_timed) : (mx ? pipe_cap_plain_max : pipe_cap_plain);
                if (cp != bound) { vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, cp); bound = cp; }
                float pc[8] = { p.cap_vp[0], p.cap_vp[1], p.cap_vp[2], 0, b.timed, b.age0, b.dtw, b.gain };
                push(c, pc, sizeof pc);
                vkCmdDraw(c, 4, b.count, 0, b.first);
            }
        }
        vkCmdEndRenderPass(c);
        float pc[32];
        auto blk = [&](const float *rest, int n) { pc[0] = pc[1] = pc[2] = pc[3] = 0; memcpy(pc + 4, rest, n * 4); return (uint32_t) (16 + n * 4); };
        if (p.phosphor) {
            // 2. the phosphor image aged and lit again, 3. what it shows
            fs(c, P2, rp_off_dc, pipe_up, set({ P.view, F.view }), pc, blk(p.update, 12));
            std::swap(P, P2);
            fs(c, D, rp_off_dc, pipe_show, set({ P.view }), pc, blk(p.show, 8));
        }
        // 4. the bloom: a quarter-size copy, blurred (H, V, H, V)
        if (p.blur_passes) fs(c, B1, rp_off_dc, pipe_copy, set({ D.view }), pc, blk(p.blur[0], 0));
        for (int i = 0; i < p.blur_passes; i++)
            fs(c, (i & 1) ? B1 : B2, rp_off_dc, pipe_blur, set({ (i & 1) ? B2.view : B1.view }), pc, blk(p.blur[i], 4));
        if (p.sheet) {
            // 5. the screen into S (upright), its light at quarter size blurred (M once, F three times), at
            // eighth size its angle harmonics
            blk(p.fin + 4, 12); pc[0] = 0; pc[1] = (float) lh; pc[2] = (float) lw; pc[3] = 0;
            fs(c, S, rp_s, pipe_fin_s, set({ D.view, B1.view }), pc, 64);
            fs(c, Q0, rp_off_dc, pipe_copy, set({ S.view }), pc, blk(p.blur[0], 0));
            for (int i = 0; i < 6; i++) {
                Img &src = i == 0 ? Q0 : (i == 2 ? SB3 : (i & 1) ? SB2 : SB1);
                Img &dst = (i & 1) ? (i == 1 ? SB3 : SB1) : SB2;
                fs(c, dst, rp_off_dc, pipe_blur, set({ src.view }), pc, blk(p.sblur[i], 4));
            }
            fs(c, Q8, rp_off_dc, pipe_copy, set({ Q0.view }), pc, blk(p.blur[0], 0));
            for (int k = 0; k < 3; k++) fs(c, Hh[k], rp_h, pipe_harm_h, set({ Q8.view }), pc, blk(p.harm_h[k], 4));
            for (int k = 0; k < 2; k++) fs(c, V[k], rp_off_dc, pipe_harm_v, set({ Hh[0].view, Hh[1].view, Hh[2].view }, true), pc, blk(p.harm_v[k], 8));
        }
        // 6. the display: the screen (curve, tone, bloom) and the overlay on it - or the sheet pass
        begin(c, rp_swap, sfb[si], (int) extent.width, (int) extent.height);
        float fvp[4] = { 0.f, (float) lh, (float) lw, (float) rot };
        if (p.sheet) {
            bool sc = scr[0].img != VK_NULL_HANDLE;
            VkDescriptorSet ds = set({ S.view, art.view, sc ? scr[0].view : art.view, sc ? scr[1].view : art.view, sc ? scr[2].view : art.view,
                                       SB3.view, SB1.view, V[0].view, V[1].view });
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_sheet);
            vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &ds, 0, nullptr);
            blk(p.sheet_pc, 20); memcpy(pc, fvp, 16);
            push(c, pc, 96);
            vkCmdDraw(c, 3, 1, 0, 0);
        } else {
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_fin);
            VkDescriptorSet ds = set({ D.view, B1.view });
            vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &ds, 0, nullptr);
            blk(p.fin + 4, 12); memcpy(pc, fvp, 16);
            push(c, pc, 64);
            vkCmdDraw(c, 3, 1, 0, 0);
            if (p.art && art.img) {
                VkDescriptorSet as = set({ art.view });
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &as, 0, nullptr);
                for (int i = 0; i < 2; i++) {
                    float a[12] = { 1.f, (float) rot, (float) lw, (float) lh };
                    memcpy(a + 4, p.art_rect, 16); memcpy(a + 8, p.art_a[i], 16);
                    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, i ? pipe_art1 : pipe_art0);
                    push(c, a, sizeof a);
                    vkCmdDraw(c, 4, 1, 0, 0);
                }
            }
        }
        vkCmdEndRenderPass(c);
        vkEndCommandBuffer(c);
        VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo sub{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        sub.waitSemaphoreCount = 1; sub.pWaitSemaphores = &acquired[fi]; sub.pWaitDstStageMask = &ws;
        sub.commandBufferCount = 1; sub.pCommandBuffers = &c;
        sub.signalSemaphoreCount = 1; sub.pSignalSemaphores = &rendered[si];
        if (vkQueueSubmit(queue, 1, &sub, fences[fi]) != VK_SUCCESS) return false;
        VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &rendered[si];
        pi.swapchainCount = 1; pi.pSwapchains = &swapchain; pi.pImageIndices = &si;
        r = vkQueuePresentKHR(queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || (r == VK_SUBOPTIMAL_KHR && turned())) { std::string e; vkDeviceWaitIdle(dev); free_swapchain(); make_swapchain(e); }
        else if (r == VK_ERROR_SURFACE_LOST_KHR || r == VK_ERROR_DEVICE_LOST) return false;
        return true;
    }

private:
    ANativeWindow *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE; VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE; VkDevice dev = VK_NULL_HANDLE; VkQueue queue = VK_NULL_HANDLE; uint32_t qfam = 0;
    VkPhysicalDeviceMemoryProperties memp{};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE; VkFormat swap_fmt = VK_FORMAT_R8G8B8A8_UNORM; VkExtent2D extent{ 0, 0 };
    // pre-rotation: the swapchain's images in the display's own orientation (extent), the picture drawn
    // turned by rot x 90 degrees clockwise into them, so the compositor shows them as they are (no extra
    // rotation pass - handhelds with a portrait panel held sideways); lw x lh = the upright window
    int rot = 0, lw = 0, lh = 0; VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    std::vector<VkImage> simg; std::vector<VkImageView> sview; std::vector<VkFramebuffer> sfb; std::vector<VkSemaphore> rendered;
    VkFormat off_fmt = VK_FORMAT_R16G16B16A16_SFLOAT, h_fmt = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkRenderPass rp_off_clear = VK_NULL_HANDLE, rp_off_dc = VK_NULL_HANDLE, rp_s = VK_NULL_HANDLE, rp_h = VK_NULL_HANDLE, rp_swap = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE, sampler_n = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; VkDescriptorPool dpool = VK_NULL_HANDLE;
    std::map<std::vector<VkImageView>, VkDescriptorSet> sets;
    VkPipeline pipe_cap_timed = VK_NULL_HANDLE, pipe_cap_timed_max = VK_NULL_HANDLE, pipe_cap_plain = VK_NULL_HANDLE, pipe_cap_plain_max = VK_NULL_HANDLE,
               pipe_up = VK_NULL_HANDLE, pipe_show = VK_NULL_HANDLE, pipe_blur = VK_NULL_HANDLE, pipe_fin = VK_NULL_HANDLE, pipe_fin_s = VK_NULL_HANDLE,
               pipe_art0 = VK_NULL_HANDLE, pipe_art1 = VK_NULL_HANDLE, pipe_scr = VK_NULL_HANDLE, pipe_copy = VK_NULL_HANDLE,
               pipe_harm_h = VK_NULL_HANDLE, pipe_harm_v = VK_NULL_HANDLE, pipe_sheet = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE; VkCommandBuffer cmds[FRAMES]{};
    VkFence fences[FRAMES]{}; VkSemaphore acquired[FRAMES]{};
    Buf inst[FRAMES];
    Img P, P2, F, D, B1, B2, art;
    Img S, Q0, SB1, SB2, SB3, Q8, Hh[3], V[2];          // (the sheet pass: see render)
    Img scr[3];                                         // (the scratch textures A, B, C)
    int tw = 0, th = 0;
    uint64_t frame = 0;

    uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want)
    {
        for (uint32_t i = 0; i < memp.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (memp.memoryTypes[i].propertyFlags & want) == want) return i;
        return 0;
    }
    void make_buf(Buf &b, size_t size, VkBufferUsageFlags use)
    {
        VkBufferCreateInfo ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; ci.size = size; ci.usage = use;
        vkCreateBuffer(dev, &ci, nullptr, &b.buf);
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, b.buf, &mr);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; ai.allocationSize = mr.size;
        ai.memoryTypeIndex = memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(dev, &ai, nullptr, &b.mem); vkBindBufferMemory(dev, b.buf, b.mem, 0);
        vkMapMemory(dev, b.mem, 0, size, 0, &b.map); b.size = size;
    }
    void free_buf(Buf &b)
    {
        if (b.buf) vkDestroyBuffer(dev, b.buf, nullptr);
        if (b.mem) vkFreeMemory(dev, b.mem, nullptr);
        b = Buf();
    }
    // an image (and a framebuffer for render pass rp, if given)
    void make_img(Img &m, int w, int h, VkFormat fmt, VkImageUsageFlags use, VkRenderPass rp)
    {
        m.w = w; m.h = h;
        VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = { (uint32_t) w, (uint32_t) h, 1 };
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = use; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(dev, &ci, nullptr, &m.img);
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, m.img, &mr);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; ai.allocationSize = mr.size;
        ai.memoryTypeIndex = memtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev, &ai, nullptr, &m.mem); vkBindImageMemory(dev, m.img, m.mem, 0);
        m.view = view(m.img, fmt);
        if (rp) {
            VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fi.renderPass = rp; fi.attachmentCount = 1; fi.pAttachments = &m.view;
            fi.width = w; fi.height = h; fi.layers = 1;
            vkCreateFramebuffer(dev, &fi, nullptr, &m.fb);
        }
    }
    void free_img(Img &m)
    {
        if (m.fb) vkDestroyFramebuffer(dev, m.fb, nullptr);
        if (m.view) vkDestroyImageView(dev, m.view, nullptr);
        if (m.img) vkDestroyImage(dev, m.img, nullptr);
        if (m.mem) vkFreeMemory(dev, m.mem, nullptr);
        m = Img();
    }
    VkImageView view(VkImage img, VkFormat fmt)
    {
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView v; vkCreateImageView(dev, &vi, nullptr, &v);
        return v;
    }
    static void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags sa, VkAccessFlags da,
                        VkPipelineStageFlags ss, VkPipelineStageFlags ds)
    {
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.oldLayout = from; b.newLayout = to; b.srcAccessMask = sa; b.dstAccessMask = da;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(c, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    VkCommandBuffer once_begin()
    {
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer c; vkAllocateCommandBuffers(dev, &ai, &c);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(c, &bi);
        return c;
    }
    void once_end(VkCommandBuffer c)
    {
        vkEndCommandBuffer(c);
        VkSubmitInfo s{ VK_STRUCTURE_TYPE_SUBMIT_INFO }; s.commandBufferCount = 1; s.pCommandBuffers = &c;
        vkQueueSubmit(queue, 1, &s, VK_NULL_HANDLE); vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(dev, pool, 1, &c);
    }
    // black and ready to be read (the first frame reads P and B1 before anything was drawn into them)
    void clear_all(std::initializer_list<Img *> imgs)
    {
        VkCommandBuffer c = once_begin();
        for (Img *m : imgs) {
            barrier(c, m->img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkClearColorValue z{}; VkImageSubresourceRange r{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(c, m->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &z, 1, &r);
            barrier(c, m->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        once_end(c);
    }

    void targets(int w, int h, int qw, int qh)
    {
        reset_sets();
        for (Img *m : { &P, &P2, &F, &D, &B1, &B2 }) free_img(*m);
        const VkImageUsageFlags use = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        make_img(P, w, h, off_fmt, use, rp_off_clear); make_img(P2, w, h, off_fmt, use, rp_off_clear);
        make_img(F, w, h, off_fmt, use, rp_off_clear); make_img(D, w, h, off_fmt, use, rp_off_clear);
        make_img(B1, qw, qh, off_fmt, use, rp_off_clear); make_img(B2, qw, qh, off_fmt, use, rp_off_clear);
        tw = w; th = h;
        clear_all({ &P, &P2, &F, &D, &B1, &B2 });
    }
    void sheet_targets(const FramePlan &p)
    {
        reset_sets();
        for (Img *m : { &S, &Q0, &SB1, &SB2, &SB3, &Q8, &Hh[0], &Hh[1], &Hh[2], &V[0], &V[1] }) free_img(*m);
        const VkImageUsageFlags use = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        make_img(S, p.screen_w, p.screen_h, VK_FORMAT_R8G8B8A8_UNORM, use, rp_s);     // (8 bits: the desktop's window)
        for (Img *m : { &Q0, &SB1, &SB2, &SB3 }) make_img(*m, p.qw4, p.qh4, off_fmt, use, rp_off_clear);
        make_img(Q8, p.qw8, p.qh8, off_fmt, use, rp_off_clear);
        for (Img &m : Hh) make_img(m, p.qw8, p.qh8, h_fmt, use, rp_h);
        for (Img &m : V) make_img(m, p.qw8, p.qh8, off_fmt, use, rp_off_clear);
        clear_all({ &S, &Q0, &SB1, &SB2, &SB3, &Q8, &Hh[0], &Hh[1], &Hh[2], &V[0], &V[1] });
    }

    void reset_sets() { if (dpool) vkResetDescriptorPool(dev, dpool, 0); sets.clear(); }
    // the descriptor set for these images at bindings 1, 2, ... (the rest: the first again); nearest: no filtering
    VkDescriptorSet set(std::initializer_list<VkImageView> views, bool nearest = false)
    {
        std::vector<VkImageView> k(views);
        k.push_back(nearest ? (VkImageView) 1 : (VkImageView) 0);
        auto it = sets.find(k);
        if (it != sets.end()) return it->second;
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = dpool; ai.descriptorSetCount = 1; ai.pSetLayouts = &dsl;
        VkDescriptorSet s = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(dev, &ai, &s) != VK_SUCCESS) { reset_sets(); vkAllocateDescriptorSets(dev, &ai, &s); }
        VkDescriptorImageInfo ii[NBIND]; VkWriteDescriptorSet w[NBIND];
        std::vector<VkImageView> v(views);
        for (int i = 0; i < NBIND; i++) {
            ii[i] = { nearest ? sampler_n : sampler, i < (int) v.size() ? v[i] : v[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            w[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w[i].dstSet = s; w[i].dstBinding = 1 + i; w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &ii[i];
        }
        vkUpdateDescriptorSets(dev, NBIND, w, 0, nullptr);
        sets[k] = s;
        return s;
    }

    void begin(VkCommandBuffer c, VkRenderPass rp, VkFramebuffer fb, int w, int h)
    {
        VkClearValue cv{}; cv.color = { { 0.f, 0.f, 0.f, 0.f } };
        VkRenderPassBeginInfo bi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        bi.renderPass = rp; bi.framebuffer = fb; bi.renderArea.extent = { (uint32_t) w, (uint32_t) h };
        bi.clearValueCount = 1; bi.pClearValues = &cv;
        vkCmdBeginRenderPass(c, &bi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{ 0, 0, (float) w, (float) h, 0, 1 }; vkCmdSetViewport(c, 0, 1, &vp);
        VkRect2D sc{ { 0, 0 }, { (uint32_t) w, (uint32_t) h } }; vkCmdSetScissor(c, 0, 1, &sc);
    }
    void push(VkCommandBuffer c, const float *d, uint32_t n)
    {
        vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, n, d);
    }
    void fs(VkCommandBuffer c, Img &to, VkRenderPass rp, VkPipeline pipe, VkDescriptorSet ds, const float *pc, uint32_t n)
    {
        begin(c, rp, to.fb, to.w, to.h);
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &ds, 0, nullptr);
        push(c, pc, n);
        vkCmdDraw(c, 3, 1, 0, 0);
        vkCmdEndRenderPass(c);
    }

    VkRenderPass render_pass(VkFormat fmt, VkAttachmentLoadOp load, bool screen)
    {
        VkAttachmentDescription a{};
        a.format = fmt; a.samples = VK_SAMPLE_COUNT_1_BIT; a.loadOp = load; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;     // (every pass clears or overwrites all of its image)
        a.finalLayout = screen ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
        VkSubpassDependency dep[2] = {};
        dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;          // (earlier passes wrote / read it)
        dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;          // (later passes read it)
        dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        if (screen) { dep[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; dep[1].dstAccessMask = 0; }
        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = 1; ci.pAttachments = &a; ci.subpassCount = 1; ci.pSubpasses = &sp; ci.dependencyCount = 2; ci.pDependencies = dep;
        VkRenderPass rp; vkCreateRenderPass(dev, &ci, nullptr, &rp);
        return rp;
    }

    bool make_swapchain(std::string &err)
    {
        VkSurfaceCapabilitiesKHR caps;
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps) != VK_SUCCESS) { err = "surface lost"; return false; }
        VkExtent2D up = caps.currentExtent;
        if (up.width == 0xFFFFFFFFu) { up.width = ANativeWindow_getWidth(window); up.height = ANativeWindow_getHeight(window); }
        if (!up.width || !up.height) { err = "the window has no size"; return false; }
        transform = caps.currentTransform;
        rot = transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? 1 : transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ? 2
            : transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR ? 3 : 0;
        if (!rot) transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        lw = (int) up.width; lh = (int) up.height;
        extent = (rot & 1) ? VkExtent2D{ up.height, up.width } : up;
        uint32_t n = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n); vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &n, fmts.data());
        swap_fmt = n ? fmts[0].format : VK_FORMAT_R8G8B8A8_UNORM;
        VkColorSpaceKHR cs = n ? fmts[0].colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        for (auto &f : fmts)                                // (UNORM: the values as computed, as OpenGL's window)
            if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { swap_fmt = f.format; cs = f.colorSpace; break; }
        VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        ci.surface = surface;
        ci.minImageCount = std::max(caps.minImageCount, 3u);
        if (caps.maxImageCount) ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
        ci.imageFormat = swap_fmt; ci.imageColorSpace = cs; ci.imageExtent = extent; ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = transform;
        ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;          // (the display's refresh paces the render thread)
        ci.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(dev, &ci, nullptr, &swapchain) != VK_SUCCESS) { err = "no swapchain"; swapchain = VK_NULL_HANDLE; return false; }
        vkGetSwapchainImagesKHR(dev, swapchain, &n, nullptr); simg.resize(n); vkGetSwapchainImagesKHR(dev, swapchain, &n, simg.data());
        if (!rp_swap) rp_swap = render_pass(swap_fmt, VK_ATTACHMENT_LOAD_OP_DONT_CARE, true);
        for (auto im : simg) {
            sview.push_back(view(im, swap_fmt));
            VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fi.renderPass = rp_swap; fi.attachmentCount = 1; fi.pAttachments = &sview.back();
            fi.width = extent.width; fi.height = extent.height; fi.layers = 1;
            VkFramebuffer fb; vkCreateFramebuffer(dev, &fi, nullptr, &fb); sfb.push_back(fb);
            VkSemaphoreCreateInfo s{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VkSemaphore sem; vkCreateSemaphore(dev, &s, nullptr, &sem); rendered.push_back(sem);
        }
        LOGI("Vulkan swapchain %ux%u, %u images, format %d, turned %d degrees", extent.width, extent.height, n, swap_fmt, rot * 90);
        return true;
    }
    bool turned()                                        // (the display turned since the swapchain was made)
    {
        VkSurfaceCapabilitiesKHR caps;
        return vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps) == VK_SUCCESS && caps.currentTransform != transform;
    }
    void free_swapchain()
    {
        for (auto f : sfb) vkDestroyFramebuffer(dev, f, nullptr);
        for (auto v : sview) vkDestroyImageView(dev, v, nullptr);
        for (auto s : rendered) vkDestroySemaphore(dev, s, nullptr);
        sfb.clear(); sview.clear(); rendered.clear(); simg.clear();
        if (swapchain) vkDestroySwapchainKHR(dev, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    VkShaderModule module(const char *nm)
    {
        const ShaderSrc &s = shader(nm);
        VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = s.spv_size; ci.pCode = s.spv;
        VkShaderModule m = VK_NULL_HANDLE; vkCreateShaderModule(dev, &ci, nullptr, &m);
        return m;
    }
    enum Blend { B_NONE, B_FRESH, B_FRESH_MAX, B_CRISP, B_MAX, B_MUL, B_ADD };
    VkPipeline pipeline(const char *vs, const char *fsn, VkRenderPass rp, Blend bl, bool instanced, VkPrimitiveTopology topo)
    {
        VkShaderModule v = module(vs), f = module(fsn);
        if (!v || !f) return VK_NULL_HANDLE;
        VkPipelineShaderStageCreateInfo st[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = v; st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = f; st[1].pName = "main";
        VkVertexInputBindingDescription vb{ 0, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE };
        VkVertexInputAttributeDescription va[2] = { { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16 } };
        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        if (instanced) { vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb; vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = va; }
        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO }; ia.topology = topo;
        VkPipelineViewportStateCreateInfo vps{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO }; vps.viewportCount = 1; vps.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;
        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        if (bl != B_NONE) {
            ba.blendEnable = VK_TRUE; ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_ADD;
            ba.srcColorBlendFactor = ba.dstColorBlendFactor = ba.srcAlphaBlendFactor = ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            switch (bl) {
            case B_FRESH: ba.alphaBlendOp = VK_BLEND_OP_MAX; break;                     // colour added, the newest life wins
            case B_FRESH_MAX: ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_MAX; break;   // (Even beam: the brighter light)
            case B_CRISP: ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; break;
            case B_MAX: ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_MAX; break;
            case B_MUL: ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
                        ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_COLOR; break;
            default: break;                                                            // (B_ADD)
            }
        }
        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO }; cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO }; ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        ci.stageCount = 2; ci.pStages = st; ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia; ci.pViewportState = &vps;
        ci.pRasterizationState = &rs; ci.pMultisampleState = &ms; ci.pColorBlendState = &cb; ci.pDynamicState = &ds;
        ci.layout = layout; ci.renderPass = rp; ci.subpass = 0;
        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
        vkDestroyShaderModule(dev, v, nullptr); vkDestroyShaderModule(dev, f, nullptr);
        return p;
    }
    bool make_pipelines(std::string &err)
    {
        rp_off_clear = render_pass(off_fmt, VK_ATTACHMENT_LOAD_OP_CLEAR, false);
        rp_off_dc = render_pass(off_fmt, VK_ATTACHMENT_LOAD_OP_DONT_CARE, false);
        rp_s = render_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_DONT_CARE, false);
        rp_h = render_pass(h_fmt, VK_ATTACHMENT_LOAD_OP_DONT_CARE, false);
        VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxLod = 0.f;
        vkCreateSampler(dev, &si, nullptr, &sampler);
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;     // (32-bit float images need not be filterable)
        vkCreateSampler(dev, &si, nullptr, &sampler_n);
        VkDescriptorSetLayoutBinding b[NBIND];
        for (int i = 0; i < NBIND; i++) b[i] = { (uint32_t) (1 + i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo dli{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO }; dli.bindingCount = NBIND; dli.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &dli, nullptr, &dsl);
        VkPushConstantRange pr{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pli.setLayoutCount = 1; pli.pSetLayouts = &dsl; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
        vkCreatePipelineLayout(dev, &pli, nullptr, &layout);
        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 * NBIND };
        VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO }; dpi.maxSets = 64; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev, &dpi, nullptr, &dpool);
        const auto strip = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, tri = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, lines = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        pipe_cap_timed = pipeline("capsule.vert", "capsule.frag", rp_off_clear, B_FRESH, true, strip);
        pipe_cap_timed_max = pipeline("capsule.vert", "capsule.frag", rp_off_clear, B_FRESH_MAX, true, strip);
        pipe_cap_plain = pipeline("capsule.vert", "capsule.frag", rp_off_clear, B_CRISP, true, strip);
        pipe_cap_plain_max = pipeline("capsule.vert", "capsule.frag", rp_off_clear, B_MAX, true, strip);
        pipe_up = pipeline("fs.vert", "update.frag", rp_off_dc, B_NONE, false, tri);
        pipe_show = pipeline("fs.vert", "show.frag", rp_off_dc, B_NONE, false, tri);
        pipe_blur = pipeline("fs.vert", "blur.frag", rp_off_dc, B_NONE, false, tri);
        pipe_fin = pipeline("fs.vert", "final.frag", rp_swap, B_NONE, false, tri);
        pipe_art0 = pipeline("art.vert", "art.frag", rp_swap, B_MUL, false, strip);
        pipe_art1 = pipeline("art.vert", "art.frag", rp_swap, B_ADD, false, strip);
        pipe_copy = pipeline("fs.vert", "copy.frag", rp_off_dc, B_NONE, false, tri);
        if (!pipe_copy || !pipe_cap_timed || !pipe_cap_timed_max || !pipe_cap_plain || !pipe_cap_plain_max || !pipe_up || !pipe_show || !pipe_blur
            || !pipe_fin || !pipe_art0 || !pipe_art1) {
            err = "a Vulkan pipeline could not be made"; return false;
        }
        // the sheet's scratches (only with float images: optional, the overlay works without them)
        if (float_images()) {
            pipe_scr = pipeline("scr_gen.vert", "scr_gen.frag", rp_off_clear, B_ADD, false, lines);
            pipe_fin_s = pipeline("fs.vert", "final.frag", rp_s, B_NONE, false, tri);
            pipe_harm_h = pipeline("fs.vert", "harm_h.frag", rp_h, B_NONE, false, tri);
            pipe_harm_v = pipeline("fs.vert", "harm_v.frag", rp_off_dc, B_NONE, false, tri);
            pipe_sheet = pipeline("fs.vert", "sheet.frag", rp_swap, B_NONE, false, tri);
        }
        sheet_ok = pipe_scr && pipe_fin_s && pipe_copy && pipe_harm_h && pipe_harm_v && pipe_sheet;
        if (!sheet_ok) LOGI("Vulkan: no overlay scratches here");
        return true;
    }
    bool sheet_ok = false;

    void destroy()
    {
        if (dev) {
            vkDeviceWaitIdle(dev);
            for (Img *m : { &P, &P2, &F, &D, &B1, &B2, &art, &S, &Q0, &SB1, &SB2, &SB3, &Q8, &Hh[0], &Hh[1], &Hh[2], &V[0], &V[1], &scr[0], &scr[1], &scr[2] })
                free_img(*m);
            for (auto &b : inst) free_buf(b);
            free_swapchain();
            for (VkPipeline p : { pipe_cap_timed, pipe_cap_timed_max, pipe_cap_plain, pipe_cap_plain_max, pipe_up, pipe_show, pipe_blur, pipe_fin,
                                  pipe_fin_s, pipe_art0, pipe_art1, pipe_scr, pipe_copy, pipe_harm_h, pipe_harm_v, pipe_sheet })
                if (p) vkDestroyPipeline(dev, p, nullptr);
            for (VkRenderPass r : { rp_off_clear, rp_off_dc, rp_s, rp_h, rp_swap }) if (r) vkDestroyRenderPass(dev, r, nullptr);
            if (dpool) vkDestroyDescriptorPool(dev, dpool, nullptr);
            if (layout) vkDestroyPipelineLayout(dev, layout, nullptr);
            if (dsl) vkDestroyDescriptorSetLayout(dev, dsl, nullptr);
            if (sampler) vkDestroySampler(dev, sampler, nullptr);
            if (sampler_n) vkDestroySampler(dev, sampler_n, nullptr);
            for (int i = 0; i < FRAMES; i++) { if (fences[i]) vkDestroyFence(dev, fences[i], nullptr); if (acquired[i]) vkDestroySemaphore(dev, acquired[i], nullptr); }
            if (pool) vkDestroyCommandPool(dev, pool, nullptr);
            vkDestroyDevice(dev, nullptr); dev = VK_NULL_HANDLE;
        }
        if (instance) {
            if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
            vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE;
        }
    }
};

}  // namespace

std::unique_ptr<Backend> make_vulkan() { return std::unique_ptr<Backend>(new Vk()); }

}  // namespace vx
