#include "goliath/imgui.hpp"
#include "imgui_.hpp"

#include "goliath/engine.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

void check_vk_result(VkResult err) {
    if (err == 0) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

namespace engine::imgui {
    ImDrawData* draw_data = nullptr;

    bool state = false;

    bool enabled() {
        return state;
    }

    void enable(bool v) {
        state = v;
    }

    void init() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window, false);
        ImGui_ImplVulkan_InitInfo imgui_info{};
        imgui_info.UseDynamicRendering = true;
        imgui_info.Instance = instance;
        imgui_info.PhysicalDevice = physical_device;
        imgui_info.Device = device;
        imgui_info.QueueFamily = graphics_queue_family;
        imgui_info.Queue = graphics_queue;
        imgui_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        imgui_info.Subpass = 0;
        imgui_info.MinImageCount = 2;
        imgui_info.ImageCount = 2;
        imgui_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        imgui_info.Allocator = nullptr;
        imgui_info.CheckVkResultFn = check_vk_result;
        imgui_info.PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapchain_format,
        };
        ImGui_ImplVulkan_Init(&imgui_info);

        state = true;
    }

    void destroy() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        state = false;
    }

    void begin() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!state) {
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
