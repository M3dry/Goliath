#include "goliath/engine.hpp"
#include "engine_.hpp"
#include "event_.hpp"
#include "goliath/samplers.hpp"
#include "materials_.hpp"
#include "models_.hpp"
#include "textures_.hpp"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <glm/ext/vector_uint2.hpp>
#include <vulkan/vulkan_core.h>

#define VOLK_IMPLEMENTATION 1
#include <volk.h>
#undef VOLK_IMPLEMENTATION

#define VMA_IMPLEMENTATION 1
#include <vk_mem_alloc.h>
#undef VMA_IMPLEMENTATION

#include "VkBootstrap.h"
#include "imgui_.hpp"
#include "transport2_.hpp"

#include <print>

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier2 image_barrier{};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier.pNext = nullptr;

    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    image_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    image_barrier.oldLayout = current_layout;
    image_barrier.newLayout = new_layout;

    VkImageAspectFlags aspect_mask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                         ? VK_IMAGE_ASPECT_DEPTH_BIT
                                         : VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange = VkImageSubresourceRange{};
    image_barrier.subresourceRange.aspectMask = aspect_mask;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    image_barrier.image = image;

    VkDependencyInfo dep_info{};
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.pNext = nullptr;

    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &image_barrier;

    vkCmdPipelineBarrier2(cmd, &dep_info);
}

namespace engine {
    GLFWwindow* window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkDevice device;
    VmaAllocator allocator;
    VkSurfaceKHR surface;

    bool updated_window_size = false;
    VkExtent2D swapchain_extent;
    VkSwapchainKHR swapchain = nullptr;
    std::vector<VkImage> swapchain_images{};
    std::vector<VkImageView> swapchain_image_views{};
    std::vector<VkSemaphore> swapchain_semaphores{};

    std::mutex graphics_queue_lock{};
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    VkQueue transport_queue;
    uint32_t transport_queue_family;

    FrameData* frames;
    uint8_t current_frame = 0;

    uint32_t swapchain_ix;

    bool models_to_save_{false};
    bool materials_to_save_{false};
    bool textures_to_save_{false};

    uint64_t timeline_value = 0;
    uint64_t presented_timeline_value = 0;
    VkSemaphore timeline_semaphore{};

    bool _drawing_prepared = false;

    VkCommandPool barriers_cmd_pool;
    VkFence barriers_cmd_buf_fence;
    VkCommandBuffer barriers_cmd_buf;

    void rebuild_swapchain(uint32_t width, uint32_t height) {
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, (int*)&width, (int*)&height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device);

        auto& frame_data = get_current_frame_data();
        frame_data.destroy_swapchain(OldSwapchain{
            .swapchain = swapchain,
            .semaphores = swapchain_semaphores,
            .last_used_timeline = timeline_value,
        });
        for (std::size_t i = 0; i < swapchain_image_views.size(); i++) {
            frame_data.destroy_view(swapchain_image_views[i]);
        }

        vkb::Swapchain vkb_swapchain =
            vkb::SwapchainBuilder{physical_device, device, surface}
                .set_desired_format(VkSurfaceFormatKHR{
                    .format = swapchain_format,
                    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                })
                .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
                .set_desired_extent(width, height)
                .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                .set_desired_min_image_count(2)
                .set_old_swapchain(swapchain)
                .build()
                .value();

        swapchain_extent = vkb_swapchain.extent;
        swapchain = vkb_swapchain.swapchain;
        swapchain_images = vkb_swapchain.get_images().value();
        swapchain_image_views = vkb_swapchain.get_image_views().value();

        VkSemaphoreCreateInfo swapchain_semaphore_info{};
        swapchain_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        swapchain_semaphore_info.pNext = nullptr;

        swapchain_semaphores.resize(swapchain_images.size());
        for (std::size_t i = 0; i < swapchain_images.size(); i++) {
            VK_CHECK(vkCreateSemaphore(device, &swapchain_semaphore_info, nullptr, &swapchain_semaphores[i]));
        }
    }

    void FrameData::cleanup_resources() {
        for (auto [buf, alloc] : buffers_to_free) {
            vmaDestroyBuffer(allocator, buf, alloc);
        }

        for (auto view : views_to_free) {
            vkDestroyImageView(device, view, nullptr);
        }

        for (auto [img, alloc] : images_to_free) {
            vmaDestroyImage(allocator, img, alloc);
        }

        for (auto sampler : samplers_to_free) {
            vkDestroySampler(device, sampler, nullptr);
        }

        std::erase_if(swapchains_to_free, [&](auto& swapchain) {
            bool destroy = swapchain.last_used_timeline < presented_timeline_value;
            if (!destroy) {
                uint64_t v = timeline_value + 1;
                VkSemaphoreWaitInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                info.semaphoreCount = 1;
                info.pSemaphores = &timeline_semaphore;
                info.pValues = &v;

                if (vkWaitSemaphores(device, &info, 0) == VK_SUCCESS) {
                    presented_timeline_value = v;
                    destroy = true;
                }
            }

            if (destroy) {
                vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);

                for (auto semaphore : swapchain.semaphores) {
                    vkDestroySemaphore(device, semaphore, nullptr);
                }
            }

            return destroy;
        });

        buffers_to_free.clear();
        images_to_free.clear();
        views_to_free.clear();
    }

    void FrameData::destroy_buffer(VkBuffer buf, VmaAllocation alloc) {
        buffers_to_free.emplace_back(buf, alloc);
    }

    void FrameData::destroy_image(VkImage img, VmaAllocation alloc) {
        images_to_free.emplace_back(img, alloc);
    }

    void FrameData::destroy_view(VkImageView view) {
        views_to_free.emplace_back(view);
    }

    void FrameData::destroy_sampler(VkSampler sampler) {
        samplers_to_free.emplace_back(sampler);
    }

    void FrameData::destroy_swapchain(OldSwapchain swapchain) {
        swapchains_to_free.emplace_back(swapchain);
    }

    FrameData::FrameData() {
        VkCommandPoolCreateInfo cmd_pool_info{};
        cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.pNext = nullptr;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmd_pool_info.queueFamilyIndex = graphics_queue_family;

        vkCreateCommandPool(device, &cmd_pool_info, nullptr, &cmd_pool);

        VkCommandBufferAllocateInfo cmd_alloc_info{};
        cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc_info.pNext = nullptr;
        cmd_alloc_info.commandPool = cmd_pool;
        cmd_alloc_info.commandBufferCount = 1;
        cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        vkAllocateCommandBuffers(device, &cmd_alloc_info, &cmd_buf);

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.pNext = nullptr;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &render_fence));

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = nullptr;

        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &swapchain_semaphore));
    }

    FrameData::~FrameData() {
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        vkDestroyFence(device, render_fence, nullptr);
        vkDestroySemaphore(device, swapchain_semaphore, nullptr);

        cleanup_resources();

        for (auto& swapchain : swapchains_to_free) {
            vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);

            for (auto semaphore : swapchain.semaphores) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
    }

    void init(Init opts) {
        VK_CHECK(volkInitialize());

        vkb::InstanceBuilder instance_builder;
        auto vkb_inst_builder = instance_builder.set_app_name("Vulkan test")
                                    .enable_validation_layers()
                                    .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                                    .use_default_debug_messenger()
                                    .require_api_version(1, 3, 0)
                                    .build();
        if (!vkb_inst_builder) {
            std::println("vkb error: {}", vkb_inst_builder.error().message());
            exit(-1);
        }

        vkb::Instance vkb_inst = vkb_inst_builder.value();
        instance = vkb_inst.instance;
        debug_messenger = vkb_inst.debug_messenger;
        volkLoadInstance(instance);

        glfwSetErrorCallback([](int err, const char* desc) { fprintf(stderr, "GLFW error %d: %s\n", err, desc); });

        assert(glfwInit() == GLFW_TRUE);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        window =
            glfwCreateWindow(mode->width, mode->height, opts.window_name, opts.fullscreen ? monitor : nullptr, nullptr);
        VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

        glfwFocusWindow(window);

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = true;
        features13.synchronization2 = true;
        // features13.robustImageAccess = true;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress = true;
        features12.descriptorIndexing = true;
        features12.descriptorBindingPartiallyBound = true;
        features12.descriptorBindingVariableDescriptorCount = true;
        features12.shaderSampledImageArrayNonUniformIndexing = true;
        features12.descriptorBindingSampledImageUpdateAfterBind = true;
        features12.descriptorBindingStorageImageUpdateAfterBind = true;
        features12.descriptorBindingUniformBufferUpdateAfterBind = true;
        features12.runtimeDescriptorArray = true;
        features12.drawIndirectCount = true;
        features12.timelineSemaphore = true;
        VkPhysicalDeviceVulkan11Features features11{};
        features11.shaderDrawParameters = true;
        VkPhysicalDeviceFeatures features{};
        features.multiDrawIndirect = true;
        features.independentBlend = true;
        // features.robustBufferAccess = true;

        auto physical_device_selected = vkb::PhysicalDeviceSelector{vkb_inst}
                                            .set_minimum_version(1, 3)
                                            .set_required_features_13(features13)
                                            .set_required_features_12(features12)
                                            .set_required_features_11(features11)
                                            .set_required_features(features)
                                            .set_surface(surface)
                                            .add_required_extensions({"VK_EXT_shader_object", "VK_EXT_robustness2"})
                                            .select();
        if (!physical_device_selected) {
            std::println("vkb error: {}", physical_device_selected.error().message());
            exit(-1);
        }

        vkb::PhysicalDevice vkb_physical_device = physical_device_selected.value();
        physical_device = vkb_physical_device.physical_device;
        vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

        VkPhysicalDeviceShaderObjectFeaturesEXT shader_object_extension{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
            .pNext = nullptr,
            .shaderObject = true,
        };
        // VkPhysicalDeviceRobustness2FeaturesEXT robustness2_extension{
        //     .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
        //     .pNext = &shader_object_extension,
        //     .robustBufferAccess2 = true,
        //     .robustImageAccess2 = true,
        //     .nullDescriptor = true,
        // };

        vkb::Device vkb_device =
            vkb::DeviceBuilder{vkb_physical_device}.add_pNext(&shader_object_extension).build().value();
        device = vkb_device.device;

        volkLoadDevice(device);

        VmaVulkanFunctions vma_vulkan_funcs{};
        vma_vulkan_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vma_vulkan_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo vma_allocator_info{};
        vma_allocator_info.physicalDevice = physical_device;
        vma_allocator_info.device = device;
        vma_allocator_info.instance = instance;
        vma_allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vma_allocator_info.pVulkanFunctions = &vma_vulkan_funcs;
        VK_CHECK(vmaCreateAllocator(&vma_allocator_info, &allocator));

        frames = new FrameData[frames_in_flight]{};
        rebuild_swapchain((uint32_t)mode->width, (uint32_t)mode->height);

        graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
        graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

        transport_queue = vkb_device.get_queue(vkb::QueueType::transfer).value();
        transport_queue_family = vkb_device.get_queue_index(vkb::QueueType::transfer).value();

        VkSemaphoreTypeCreateInfo timeline_semaphore_type{};
        timeline_semaphore_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timeline_semaphore_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_semaphore_type.initialValue = presented_timeline_value = timeline_value;

        VkSemaphoreCreateInfo timeline_semaphore_info{};
        timeline_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        timeline_semaphore_info.pNext = &timeline_semaphore_type;

        VK_CHECK(vkCreateSemaphore(device, &timeline_semaphore_info, nullptr, &timeline_semaphore));

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_queue_family;
        VK_CHECK(vkCreateCommandPool(device, &pool_info, nullptr, &barriers_cmd_pool));

        VkCommandBufferAllocateInfo cmd_buf_alloc_info{};
        cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buf_alloc_info.commandBufferCount = 1;
        cmd_buf_alloc_info.commandPool = barriers_cmd_pool;
        cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_CHECK(vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &barriers_cmd_buf));

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.pNext = nullptr;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &barriers_cmd_buf_fence));

        samplers::init();
        transport2::init();
        imgui::init();
        event::register_glfw_callbacks();
        descriptor::create_empty_set();
        if (opts.textures_directory) textures::init(opts.texture_capacity, *opts.textures_directory);
        if (opts.models_directory) {
            materials::init();
            models::init(*opts.models_directory);
        }

        glfwShowWindow(window);
    };

    void destroy() {
        vkDeviceWaitIdle(device);

        materials::destroy();
        models::destroy();
        textures::destroy();
        samplers::destroy();
        descriptor::destroy_empty_set();
        imgui::destroy();
        transport2::destroy();

        vkDestroyCommandPool(device, barriers_cmd_pool, nullptr);
        vkDestroyFence(device, barriers_cmd_buf_fence, nullptr);

        delete[] frames;

        vkDestroySemaphore(device, timeline_semaphore, nullptr);

        VmaTotalStatistics stats;
        vmaCalculateStatistics(allocator, &stats);
        if (stats.total.statistics.allocationCount != 0) {
            char* stats_str = nullptr;
            vmaBuildStatsString(allocator, &stats_str, VK_TRUE);
            printf("%s\n", stats_str);
            vmaFreeStatsString(allocator, stats_str);
        }

        vmaDestroyAllocator(allocator);

        vkDestroySwapchainKHR(device, swapchain, nullptr);
        for (std::size_t i = 0; i < swapchain_image_views.size(); i++) {
            vkDestroyImageView(device, swapchain_image_views[i], nullptr);
            vkDestroySemaphore(device, swapchain_semaphores[i], nullptr);
        }

        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkb::destroy_debug_utils_messenger(instance, debug_messenger);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    FrameData& get_current_frame_data() {
        return frames[current_frame];
    }

    VkCommandBuffer get_cmd_buf() {
        return get_current_frame_data().cmd_buf;
    }

    VkImage get_swapchain() {
        return swapchain_images[swapchain_ix];
    }

    VkImageView get_swapchain_view() {
        return swapchain_image_views[swapchain_ix];
    }

    bool prepare_frame() {
        FrameData& frame = get_current_frame_data();

        VK_CHECK(vkWaitForFences(device, 1, &frame.render_fence, true, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &frame.render_fence));

        frame.cleanup_resources();

        bool rebuilt = false;
        while (true) {
            auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.swapchain_semaphore,
                                                VK_NULL_HANDLE, &swapchain_ix);
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || updated_window_size) {
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                rebuild_swapchain((uint32_t)width, (uint32_t)height);

                vkDestroySemaphore(device, frame.swapchain_semaphore, nullptr);

                VkSemaphoreCreateInfo semaphore_info{};
                semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame.swapchain_semaphore));

                updated_window_size = false;
                rebuilt = true;
                continue;
            } else {
                VK_CHECK(result);
            }

            break;
        }

        frame.render_semaphore = swapchain_ix;
        frame.descriptor_pool.clear();

        return rebuilt;
    }

    void prepare_draw() {
        VkCommandBuffer cmd_buf = get_cmd_buf();
        vkResetCommandBuffer(cmd_buf, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

        transition_image(cmd_buf, swapchain_images[swapchain_ix], VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        textures_to_save_ |= textures::process_uploads();
        models_to_save_ |= models::process_uploads();

        VkBufferMemoryBarrier2 material_barrier{};
        materials_to_save_ |= materials::update_gpu_buffer();

        _drawing_prepared = true;
    }

    bool next_frame(std::span<VkSemaphoreSubmitInfo> extra_waits) {
        FrameData& frame = get_current_frame_data();

        transition_image(frame.cmd_buf, swapchain_images[swapchain_ix], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        _drawing_prepared = false;
        VK_CHECK(vkEndCommandBuffer(frame.cmd_buf));

        VkSemaphoreSubmitInfo extra_wait_space{};
        if (extra_waits.size() == 0) {
            extra_waits = {&extra_wait_space, 1};
        }
        extra_waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        extra_waits[0].semaphore = frame.swapchain_semaphore;
        extra_waits[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSemaphoreSubmitInfo signal_info[2]{};
        signal_info[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_info[0].semaphore = swapchain_semaphores[frame.render_semaphore];
        signal_info[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        signal_info[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_info[1].semaphore = timeline_semaphore;
        signal_info[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signal_info[1].value = ++timeline_value;

        VkCommandBufferSubmitInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmd_info.commandBuffer = frame.cmd_buf;

        VkSubmitInfo2 submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_info.waitSemaphoreInfoCount = extra_waits.size();
        submit_info.pWaitSemaphoreInfos = extra_waits.data();
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &cmd_info;
        submit_info.signalSemaphoreInfoCount = 2;
        submit_info.pSignalSemaphoreInfos = signal_info;


        VkResult result;
        {
            std::lock_guard lock{graphics_queue_lock};
            VK_CHECK(vkQueueSubmit2(graphics_queue, 1, &submit_info, frame.render_fence));

            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &swapchain_semaphores[frame.render_semaphore];
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &swapchain;
            present_info.pImageIndices = &swapchain_ix;
            result = vkQueuePresentKHR(graphics_queue, &present_info);
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            rebuild_swapchain((uint32_t)width, (uint32_t)height);

            return true;
        }

        increment_frame();
        return false;
    }

    void increment_frame() {
        current_frame = (current_frame + 1) % frames_in_flight;
    }

    DescriptorPool& get_frame_descriptor_pool() {
        return get_current_frame_data().descriptor_pool;
    }

    void destroy_buffer(VkBuffer buf, VmaAllocation alloc) {
        get_current_frame_data().destroy_buffer(buf, alloc);
    }

    void destroy_image(VkImage img, VmaAllocation alloc) {
        get_current_frame_data().destroy_image(img, alloc);
    }

    void destroy_view(VkImageView view) {
        get_current_frame_data().destroy_view(view);
    }

    void destroy_sampler(VkSampler sampler) {
        get_current_frame_data().destroy_sampler(sampler);
    }

    void new_window_size(uint32_t width, uint32_t height) {
        updated_window_size = true;
    }

    uint32_t get_current_frame() {
        return current_frame;
    }

    bool models_to_save() {
        auto res = models_to_save_;
        models_to_save_ = false;
        return res;
    }

    bool materials_to_save() {
        auto res = materials_to_save_;
        materials_to_save_ = false;
        return res;
    }

    bool textures_to_save() {
        auto res = textures_to_save_;
        textures_to_save_ = false;
        return res;
    }

    bool drawing_prepared() {
        return _drawing_prepared;
    }
}
