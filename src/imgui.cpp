#include "goliath/imgui.hpp"
#include "engine_.hpp"
#include "imgui_.hpp"

#include "goliath/engine.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include <vulkan/vulkan_core.h>

void check_vk_result(VkResult err) {
    if (err == 0) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

namespace engine::imgui {
    ImDrawData* draw_data = nullptr;

    bool enabled_ = false;

    bool enabled() {
        return enabled_;
    }

    void enable(bool v) {
        enabled_ = v;

        if (!enabled_) {
            auto& io = ImGui::GetIO();
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        }
    }

    void init() {
        assert(!shared());

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(state->window, false);
        ImGui_ImplVulkan_InitInfo imgui_info{};
        imgui_info.UseDynamicRendering = true;
        imgui_info.Instance = state->instance;
        imgui_info.PhysicalDevice = state->physical_device;
        imgui_info.Device = device();
        imgui_info.QueueFamily = state->graphics_queue_family;
        imgui_info.Queue = state->graphics_queue;
        imgui_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        imgui_info.MinImageCount = 2;
        imgui_info.ImageCount = get_swapchain_count();
        imgui_info.Allocator = nullptr;
        imgui_info.CheckVkResultFn = check_vk_result;
        imgui_info.PipelineInfoMain = ImGui_ImplVulkan_PipelineInfo{
            .Subpass = 0,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            .PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &SwapchainState::format,
            },
        };
        ImGui_ImplVulkan_Init(&imgui_info);

        enabled_ = true;
    }

    void destroy() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        enabled_ = false;
    }

    void begin() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!enabled_) {
            auto& io = ImGui::GetIO();
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        }
    }

    void end() {
        ImGui::Render();
        draw_data = ImGui::GetDrawData();
    }

    void render() {
        if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) return;

        ImGui_ImplVulkan_RenderDrawData(draw_data, get_cmd_buf());
    }
}
