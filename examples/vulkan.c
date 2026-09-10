#include "nativekit.h"
#include "nativekit_vulkan.h"
#include "nativekit_window.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define FRAMES_IN_FLIGHT 2

typedef struct renderer {
    nk_handle window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    uint32_t queue_family;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer commands[FRAMES_IN_FLIGHT];
    VkSemaphore acquired[FRAMES_IN_FLIGHT];
    VkSemaphore complete[FRAMES_IN_FLIGHT];
    VkFence fences[FRAMES_IN_FLIGHT];
    uint32_t frame;
    int resized;
} renderer;

static void vk_error(const char *what, VkResult result) {
    fprintf(stderr, "%s failed (VkResult %d)\n", what, (int)result);
}

static int find_present_queue(VkPhysicalDevice device, VkSurfaceKHR surface,
                              uint32_t *out_family) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *properties = calloc(count, sizeof(*properties));
    if (!properties)
        return 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);
    int found = 0;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present) == VK_SUCCESS &&
            present) {
            *out_family = i;
            found = 1;
            break;
        }
    }
    free(properties);
    return found;
}

static int select_physical_device(renderer *r) {
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(r->instance, &count, NULL) != VK_SUCCESS || !count)
        return 0;
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices)
        return 0;
    vkEnumeratePhysicalDevices(r->instance, &count, devices);
    for (uint32_t i = 0; i < count; ++i) {
        if (find_present_queue(devices[i], r->surface, &r->queue_family)) {
            r->physical = devices[i];
            break;
        }
    }
    free(devices);
    return r->physical != VK_NULL_HANDLE;
}

static int create_device(renderer *r) {
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = r->queue_family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions};
    VkResult result = vkCreateDevice(r->physical, &info, NULL, &r->device);
    if (result != VK_SUCCESS) {
        vk_error("vkCreateDevice", result);
        return 0;
    }
    vkGetDeviceQueue(r->device, r->queue_family, 0, &r->queue);
    return 1;
}

static VkShaderModule load_shader(renderer *r, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Could not open shader: %s\n", path);
        return VK_NULL_HANDLE;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return VK_NULL_HANDLE;
    }
    long length = ftell(file);
    rewind(file);
    if (length <= 0 || (length % 4) != 0) {
        fprintf(stderr, "Invalid SPIR-V file: %s\n", path);
        fclose(file);
        return VK_NULL_HANDLE;
    }
    uint32_t *code = malloc((size_t)length);
    if (!code || fread(code, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Could not read shader: %s\n", path);
        free(code);
        fclose(file);
        return VK_NULL_HANDLE;
    }
    fclose(file);
    const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                           .codeSize = (size_t)length, .pCode = code};
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(r->device, &info, NULL, &module);
    free(code);
    if (result != VK_SUCCESS)
        vk_error("vkCreateShaderModule", result);
    return module;
}

static VkSurfaceFormatKHR choose_format(renderer *r) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical, r->surface, &count, NULL);
    VkSurfaceFormatKHR *formats = calloc(count, sizeof(*formats));
    vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical, r->surface, &count, formats);
    VkSurfaceFormatKHR selected = formats[0];
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selected = formats[i];
            break;
        }
    }
    free(formats);
    return selected;
}

static VkPresentModeKHR choose_present_mode(renderer *r) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(r->physical, r->surface, &count, NULL);
    VkPresentModeKHR *modes = calloc(count, sizeof(*modes));
    vkGetPhysicalDeviceSurfacePresentModesKHR(r->physical, r->surface, &count, modes);
    VkPresentModeKHR selected = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; ++i)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            selected = modes[i];
            break;
        }
    free(modes);
    return selected;
}

static VkExtent2D choose_extent(renderer *r, const VkSurfaceCapabilitiesKHR *caps) {
    if (caps->currentExtent.width != UINT32_MAX)
        return caps->currentExtent;
    int32_t width = 1, height = 1;
    nk_window_get_framebuffer_size(r->window, &width, &height);
    VkExtent2D extent = {(uint32_t)(width > 0 ? width : 1),
                         (uint32_t)(height > 0 ? height : 1)};
    if (extent.width < caps->minImageExtent.width) extent.width = caps->minImageExtent.width;
    if (extent.width > caps->maxImageExtent.width) extent.width = caps->maxImageExtent.width;
    if (extent.height < caps->minImageExtent.height) extent.height = caps->minImageExtent.height;
    if (extent.height > caps->maxImageExtent.height) extent.height = caps->maxImageExtent.height;
    return extent;
}

static void destroy_swapchain(renderer *r) {
    for (uint32_t i = 0; i < r->image_count; ++i) {
        if (r->framebuffers) vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL);
        if (r->views) vkDestroyImageView(r->device, r->views[i], NULL);
    }
    if (r->pipeline) vkDestroyPipeline(r->device, r->pipeline, NULL);
    if (r->pipeline_layout) vkDestroyPipelineLayout(r->device, r->pipeline_layout, NULL);
    if (r->render_pass) vkDestroyRenderPass(r->device, r->render_pass, NULL);
    if (r->swapchain) vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    free(r->framebuffers); free(r->views); free(r->images);
    r->framebuffers = NULL; r->views = NULL; r->images = NULL;
    r->pipeline = VK_NULL_HANDLE; r->pipeline_layout = VK_NULL_HANDLE;
    r->render_pass = VK_NULL_HANDLE; r->swapchain = VK_NULL_HANDLE; r->image_count = 0;
}

static int create_pipeline(renderer *r) {
    const VkAttachmentDescription color = {
        .format = r->format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    const VkAttachmentReference reference = {.attachment = 0,
                                              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           .colorAttachmentCount = 1,
                                           .pColorAttachments = &reference};
    const VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    const VkRenderPassCreateInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
        .pAttachments = &color, .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency};
    if (vkCreateRenderPass(r->device, &pass_info, NULL, &r->render_pass) != VK_SUCCESS)
        return 0;

    VkShaderModule vertex = load_shader(r, NATIVEKIT_VERTEX_SPV);
    VkShaderModule fragment = load_shader(r, NATIVEKIT_FRAGMENT_SPV);
    if (!vertex || !fragment) {
        if (vertex) vkDestroyShaderModule(r->device, vertex, NULL);
        if (fragment) vkDestroyShaderModule(r->device, fragment, NULL);
        return 0;
    }
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment};
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states};
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkResult result = vkCreatePipelineLayout(r->device, &layout_info, NULL, &r->pipeline_layout);
    if (result == VK_SUCCESS) {
        const VkGraphicsPipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &assembly, .pViewportState = &viewport,
            .pRasterizationState = &raster, .pMultisampleState = &multisample,
            .pColorBlendState = &blend, .pDynamicState = &dynamic,
            .layout = r->pipeline_layout, .renderPass = r->render_pass, .subpass = 0};
        result = vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &info, NULL, &r->pipeline);
    }
    vkDestroyShaderModule(r->device, vertex, NULL);
    vkDestroyShaderModule(r->device, fragment, NULL);
    if (result != VK_SUCCESS) vk_error("graphics pipeline creation", result);
    return result == VK_SUCCESS;
}

static int create_swapchain(renderer *r) {
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical, r->surface, &caps) != VK_SUCCESS)
        return 0;
    VkSurfaceFormatKHR surface_format = choose_format(r);
    r->format = surface_format.format;
    r->extent = choose_extent(r, &caps);
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    const VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = r->surface,
        .minImageCount = count, .imageFormat = r->format,
        .imageColorSpace = surface_format.colorSpace, .imageExtent = r->extent,
        .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = choose_present_mode(r), .clipped = VK_TRUE};
    VkResult result = vkCreateSwapchainKHR(r->device, &info, NULL, &r->swapchain);
    if (result != VK_SUCCESS) { vk_error("vkCreateSwapchainKHR", result); return 0; }
    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->image_count, NULL);
    r->images = calloc(r->image_count, sizeof(*r->images));
    r->views = calloc(r->image_count, sizeof(*r->views));
    r->framebuffers = calloc(r->image_count, sizeof(*r->framebuffers));
    if (!r->images || !r->views || !r->framebuffers) return 0;
    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->image_count, r->images);
    for (uint32_t i = 0; i < r->image_count; ++i) {
        const VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->format,
            .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        if (vkCreateImageView(r->device, &view_info, NULL, &r->views[i]) != VK_SUCCESS) return 0;
    }
    if (!create_pipeline(r)) return 0;
    for (uint32_t i = 0; i < r->image_count; ++i) {
        const VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = r->render_pass,
            .attachmentCount = 1, .pAttachments = &r->views[i],
            .width = r->extent.width, .height = r->extent.height, .layers = 1};
        if (vkCreateFramebuffer(r->device, &framebuffer_info, NULL, &r->framebuffers[i]) != VK_SUCCESS)
            return 0;
    }
    fprintf(stderr, "Swapchain: %ux%u, %u images\n", r->extent.width, r->extent.height,
            r->image_count);
    return 1;
}

static int recreate_swapchain(renderer *r) {
    int32_t width = 0, height = 0;
    if (nk_window_get_framebuffer_size(r->window, &width, &height) != NK_OK || width <= 0 || height <= 0)
        return 1;
    vkDeviceWaitIdle(r->device);
    destroy_swapchain(r);
    r->resized = 0;
    return create_swapchain(r);
}

static int record_commands(renderer *r, uint32_t image) {
    VkCommandBuffer command = r->commands[r->frame];
    vkResetCommandBuffer(command, 0);
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) return 0;
    const VkClearValue clear = {.color = {{0.035f, 0.045f, 0.07f, 1.0f}}};
    const VkRenderPassBeginInfo pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = r->render_pass,
        .framebuffer = r->framebuffers[image], .renderArea = {{0, 0}, r->extent},
        .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
    const VkViewport viewport = {0.0f, 0.0f, (float)r->extent.width, (float)r->extent.height,
                                 0.0f, 1.0f};
    const VkRect2D scissor = {{0, 0}, r->extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
    return vkEndCommandBuffer(command) == VK_SUCCESS;
}

static int draw_frame(renderer *r) {
    vkWaitForFences(r->device, 1, &r->fences[r->frame], VK_TRUE, UINT64_MAX);
    uint32_t image = 0;
    VkResult result = vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX,
                                            r->acquired[r->frame], VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return recreate_swapchain(r);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { vk_error("acquire", result); return 0; }
    vkResetFences(r->device, 1, &r->fences[r->frame]);
    if (!record_commands(r, image)) return 0;
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
        .pWaitSemaphores = &r->acquired[r->frame], .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &r->commands[r->frame],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &r->complete[r->frame]};
    if (vkQueueSubmit(r->queue, 1, &submit, r->fences[r->frame]) != VK_SUCCESS) return 0;
    const VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
        .pWaitSemaphores = &r->complete[r->frame], .swapchainCount = 1,
        .pSwapchains = &r->swapchain, .pImageIndices = &image};
    result = vkQueuePresentKHR(r->queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || r->resized) {
        if (!recreate_swapchain(r)) return 0;
    } else if (result != VK_SUCCESS) { vk_error("present", result); return 0; }
    r->frame = (r->frame + 1) % FRAMES_IN_FLIGHT;
    return 1;
}

static int renderer_init(renderer *r) {
    if (!select_physical_device(r)) { fprintf(stderr, "No Vulkan device can present here.\n"); return 0; }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(r->physical, &properties);
    fprintf(stderr, "GPU: %s; presentation queue: %u\n", properties.deviceName, r->queue_family);
    char title[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE + 40];
    snprintf(title, sizeof(title), "NativeKit Vulkan triangle — %s", properties.deviceName);
    nk_window_set_title(r->window, title);
    if (!create_device(r)) return 0;
    const VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = r->queue_family};
    if (vkCreateCommandPool(r->device, &pool, NULL, &r->command_pool) != VK_SUCCESS) return 0;
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = FRAMES_IN_FLIGHT};
    if (vkAllocateCommandBuffers(r->device, &allocate, r->commands) != VK_SUCCESS) return 0;
    const VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                     .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        if (vkCreateSemaphore(r->device, &semaphore, NULL, &r->acquired[i]) != VK_SUCCESS ||
            vkCreateSemaphore(r->device, &semaphore, NULL, &r->complete[i]) != VK_SUCCESS ||
            vkCreateFence(r->device, &fence, NULL, &r->fences[i]) != VK_SUCCESS) return 0;
    return create_swapchain(r);
}

static void renderer_destroy(renderer *r) {
    if (!r->device) return;
    vkDeviceWaitIdle(r->device);
    destroy_swapchain(r);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (r->fences[i]) vkDestroyFence(r->device, r->fences[i], NULL);
        if (r->complete[i]) vkDestroySemaphore(r->device, r->complete[i], NULL);
        if (r->acquired[i]) vkDestroySemaphore(r->device, r->acquired[i], NULL);
    }
    if (r->command_pool) vkDestroyCommandPool(r->device, r->command_pool, NULL);
    vkDestroyDevice(r->device, NULL);
}

int main(void) {
    int exit_code = 1;
    nk_init_options init = {.struct_size = sizeof(init), .api_version = NK_API_VERSION};
    if (nk_init(&init) != NK_OK) { fprintf(stderr, "nk_init failed: %s\n", nk_last_error()); return 1; }
    if (!(nk_get_capabilities() & NK_CAP_VULKAN_SURFACE) || !nk_vulkan_supported()) {
        fprintf(stderr, "This backend does not provide Vulkan surfaces.\n"); nk_shutdown(); return 1;
    }
    nk_window_options options = {.struct_size = sizeof(options), .flags = NK_WINDOW_RESIZABLE,
                                 .width = 800, .height = 600,
                                 .title = "NativeKit Vulkan triangle"};
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&options, &window) != NK_OK) {
        fprintf(stderr, "nk_window_create failed: %s\n", nk_last_error()); nk_shutdown(); return 1;
    }
    uint32_t extension_count = 0;
    nk_vulkan_get_required_instance_extensions(window, NULL, &extension_count);
    const char **extensions = calloc(extension_count, sizeof(*extensions));
    if (!extensions || nk_vulkan_get_required_instance_extensions(window, extensions,
                                                                    &extension_count) != NK_OK)
        goto window_cleanup;
    const VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "NativeKit Vulkan triangle", .applicationVersion = 1,
        .pEngineName = "NativeKit", .engineVersion = 1, .apiVersion = VK_API_VERSION_1_0};
    const VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application, .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&instance_info, NULL, &instance);
    free(extensions);
    if (result != VK_SUCCESS) { vk_error("vkCreateInstance", result); goto window_cleanup; }
    nk_vulkan_surface native_surface = NK_INVALID_VULKAN_SURFACE;
    if (nk_vulkan_create_surface(window, (void *)instance, NULL, &native_surface) != NK_OK) {
        fprintf(stderr, "nk_vulkan_create_surface failed: %s\n", nk_last_error());
        vkDestroyInstance(instance, NULL); goto window_cleanup;
    }
    renderer graphics = {.window = window, .instance = instance,
                         .surface = (VkSurfaceKHR)(uintptr_t)native_surface};
    if (renderer_init(&graphics)) {
        int running = 1;
        while (running) {
            nk_event event = {.struct_size = sizeof(event)};
            do {
                if (nk_poll_event(&event) != NK_OK) { running = 0; break; }
                if (event.source == window && event.kind == NK_EVENT_WINDOW_CLOSE) running = 0;
                if (event.source == window && (event.kind == NK_EVENT_WINDOW_RESIZE ||
                    event.kind == NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE)) graphics.resized = 1;
                int empty = event.kind == NK_EVENT_NONE;
                nk_event_release(&event);
                if (empty) break;
                event.struct_size = sizeof(event);
            } while (running);
            if (running && !draw_frame(&graphics)) running = 0;
        }
        exit_code = 0;
    }
    renderer_destroy(&graphics);
    nk_vulkan_destroy_surface((void *)instance, native_surface, NULL);
    vkDestroyInstance(instance, NULL);
window_cleanup:
    nk_window_destroy(window);
    nk_shutdown();
    return exit_code;
}
