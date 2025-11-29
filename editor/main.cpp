#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/compute.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/imgui.hpp"
#include "goliath/model.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/scene.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_registry.hpp"
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
#include "goliath/visbuffer.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unistd.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <nfd.h>

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#define GLFW_EXPOSE_NATIVE_EGL

#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_EGL
#elif _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#endif

#include <nfd_glfw3.h>

void update_depth(engine::GPUImage* images, VkImageView* image_views, VkImageMemoryBarrier2* barriers,
                  uint32_t frames_in_flight) {
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        auto [depth_img, barrier] = engine::GPUImage::upload(engine::GPUImageInfo{}
                                                                 .format(VK_FORMAT_D16_UNORM)
                                                                 .width(engine::swapchain_extent.width)
                                                                 .height(engine::swapchain_extent.height)
                                                                 .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                                                                 .new_layout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                                                 .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

        barrier.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        images[i] = depth_img;
        barriers[i] = barrier;
        image_views[i] = engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT).create();
    }
}

void update_target(engine::GPUImage* images, VkImageView* image_views, VkImageMemoryBarrier2* barriers,
                   uint32_t frames_in_flight) {
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        auto [target_img, barrier] =
            engine::GPUImage::upload(engine::GPUImageInfo{}
                                         .format(VK_FORMAT_R32G32B32A32_SFLOAT)
                                         .width(engine::swapchain_extent.width)
                                         .height(engine::swapchain_extent.height)
                                         .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                         .new_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                                         .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        images[i] = target_img;
        barriers[i] = barrier;
        image_views[i] = engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();
    }
}

using ModelPC = engine::PushConstant<uint64_t, uint64_t, glm::mat4, glm::mat4>;
using ScenePC = engine::PushConstant<uint64_t, uint64_t, glm::mat4, glm::mat4>;
using VisbufferRasterPC = engine::PushConstant<uint64_t, engine::util::padding64, glm::mat4>;
using FlattenDrawPC =
    engine::PushConstant<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t>;
using CullingPC = engine::PushConstant<uint64_t, uint64_t, uint64_t, uint64_t, uint32_t>;
using PBRPC = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t>;
using PostprocessingPC = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t>;

struct PBRShadingSet {
    glm::vec3 cam_pos;
    float _1;
    glm::vec3 light_pos;
    float _2;
    glm::vec3 light_intensity;
    float _3;
    glm::mat4 view_proj_matrix;
};

struct Model {
    enum DataType {
        GLTF,
        GLB,
        GOM,
    };

    struct Instance {
        std::string name;

        glm::vec3 translate{0.0f};
        glm::vec3 rotate{0.0f};
        glm::vec3 scale{1.0f};
    };
    uint64_t last_instance_id = 0;

    std::string name;
    std::filesystem::path filepath;
    bool embed_optimized = false;

    uint64_t timeline;
    engine::Model cpu_data;
    engine::GPUModel gpu_data;
    engine::Buffer indirect_draw_buffer;
    engine::GPUGroup gpu_group;

    std::vector<Instance> instances{};
    std::vector<glm::mat4> instance_transforms{};

    engine::Model::Err load(const std::string& cwd, DataType type, uint8_t* data, uint32_t size,
                            VkBufferMemoryBarrier2* barrier) {
        engine::Model::Err err;
        if (type == GOM) {
            err = engine::Model::load_optimized(&cpu_data, data) ? engine::Model::Ok : engine::Model::InvalidFormat;
        } else if (type == GLTF) {
            err = engine::Model::load_gltf(&cpu_data, {data, size}, cwd);
        } else if (type == GLB) {
            err = engine::Model::load_glb(&cpu_data, {data, size}, cwd);
        } else {
            err = engine::Model::InvalidFormat;
        }

        if (err != engine::Model::Ok) {
            return err;
        }

        engine::gpu_group::begin();
        auto [_gpu_data, _indirect_draw_buffer] = engine::model::upload(&cpu_data);
        gpu_data = _gpu_data;
        indirect_draw_buffer = _indirect_draw_buffer;
        gpu_group = engine::gpu_group::end(barrier, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);

        return engine::Model::Ok;
    }

    void destroy() {
        cpu_data.destroy();
        gpu_group.destroy();
        indirect_draw_buffer.destroy();
    }
};

bool imgui_model_instance(Model::Instance& instance, glm::mat4& transform) {
    bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanLabelWidth);

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
    ImGui::InputText("##name", &instance.name);

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
    if (ImGui::Button("Remove")) {
        if (open) ImGui::TreePop();
        return true;
    }

    if (open) {
        ImGui::DragFloat3("XYZ", glm::value_ptr(instance.translate), 0.1f, 0.0f, 0.0f, "%.2f");

        ImGui::DragFloat("yaw", &instance.rotate.x, 0.1f, 0.0, 0.0, "%.2f");
        ImGui::DragFloat("pitch", &instance.rotate.y, 0.1f, 0.0, 0.0, "%.2f");
        ImGui::DragFloat("roll", &instance.rotate.z, 0.1f, 0.0, 0.0, "%.2f");

        ImGui::DragFloat3("scale", glm::value_ptr(instance.scale), 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::TreePop();
    }

    transform = glm::translate(glm::identity<glm::mat4>(), instance.translate) *
                glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(), instance.rotate.x, glm::vec3{0, 1, 0}),
                                        instance.rotate.y, glm::vec3{1, 0, 0}),
                            instance.rotate.z, glm::vec3{0, 0, 1}) *
                glm::scale(glm::identity<glm::mat4>(), instance.scale);

    return false;
}

// <model count><total instance count><external count><names size>{<is external><name metadata><name data|if <name
// metadata> valid><instance count><instance transforms><model data size><model data> |*model count}
// <model data> = <embedded||GLB||GLTF>(<binary embeded data> || <file path>)
uint8_t* serialize_scene(const Model* models, uint32_t models_size, uint32_t* out_size) {
    uint32_t total_size = sizeof(uint32_t) /* <model count> */ + sizeof(uint32_t) /* <total instance count> */ +
                          sizeof(uint32_t) /* <external count> */ + sizeof(uint32_t) /* <names size> */ +
                          sizeof(uint32_t) * models_size /* <name metadata>s */ +
                          sizeof(uint32_t) * models_size /* <instance count>s */;
    uint32_t total_instance_count = 0;
    uint32_t names_size = 0;

    for (uint32_t i = 0; i < models_size; i++) {
        total_size += sizeof(uint8_t); // <is external>

        auto& model = models[i];
        names_size += model.name.size() + 1;

        total_size += sizeof(uint8_t); // <embedded||GLB||GLTF>
        if (model.embed_optimized) {
            // total_size += model.cpu_data.; // <model data size>
        } else {
            total_size += sizeof(uint32_t);                   // <filepath size>
            total_size += model.filepath.string().size() + 1; // <model data size>
        }

        total_instance_count += model.instances.size();
    }
    total_size += total_instance_count * sizeof(glm::mat4); // <instance transforms>s
    total_size += names_size;                               // <names size>

    uint8_t* out = (uint8_t*)malloc(total_size);

    uint32_t offset = 0;
    std::memcpy(out + offset, &models_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memcpy(out + offset, &total_instance_count, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memset(out + offset, 0, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memcpy(out + offset, &names_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    for (std::size_t i = 0; i < models_size; i++) {
        auto& model = models[i];

        std::memset(out + offset, 0, sizeof(uint8_t));
        offset += sizeof(uint8_t);

        uint32_t name_size = model.name.size() + 1;
        std::memcpy(out + offset, &name_size, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(out + offset, model.name.c_str(), name_size);
        offset += name_size;

        uint32_t instance_count = model.instances.size();
        std::memcpy(out + offset, &instance_count, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(out + offset, model.instance_transforms.data(),
                    model.instance_transforms.size() * sizeof(glm::mat4));
        offset += model.instance_transforms.size() * sizeof(glm::mat4);

        if (model.embed_optimized) {
            std::memset(out + offset, 0, sizeof(uint8_t));
            offset += sizeof(uint8_t);

        } else {
            uint8_t model_type = model.filepath.extension() == ".glb" ? 1 : 2;
            std::memcpy(out + offset, &model_type, sizeof(uint8_t));
            offset += sizeof(uint8_t);

            uint32_t filepath_size = model.filepath.string().size() + 1;
            std::memcpy(out + offset, &filepath_size, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            std::memcpy(out + offset, model.filepath.c_str(), filepath_size);
            offset += filepath_size;
        }
    }

    printf("total size: %d\n", total_size);
    printf("offset: %d\n", offset);
    *out_size = total_size;
    return out;
}

struct Scene {
    std::string name;
    std::filesystem::path filepath;

    engine::Scene cpu_data;

    uint64_t timeline;
    engine::GPUGroup gpu_group;
    engine::GPUScene gpu_data;

    glm::vec3 translate{0.0f};
    glm::vec3 rotate{0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 transform = glm::identity<glm::mat4>();

    void load(const char* path, VkBufferMemoryBarrier2* indirect_buffer_barrier,
              VkBufferMemoryBarrier2* model_barrier) {
        uint32_t file_size;
        auto file = engine::util::read_file(path, &file_size);

        engine::Scene::load(&cpu_data, file, file_size);

        timeline = engine::transport::begin();
        engine::gpu_group::begin();
        engine::GPUScene::upload(&gpu_data, &cpu_data, indirect_buffer_barrier);
        gpu_group = engine::gpu_group::end(model_barrier, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
        engine::transport::end();

        free(file);
    }

    void destroy() {
        cpu_data.destroy();
        gpu_group.destroy();
        gpu_data.destroy();
    }
};

int main(int argc, char** argv) {
    std::vector<Model> models{};
    std::vector<std::vector<Model>> models_to_destroy{};
    models_to_destroy.resize(engine::frames_in_flight);

    std::vector<Scene> scenes{};

    engine::init("Goliath editor", 1000, false);
    NFD_Init();

    VkImageMemoryBarrier2* visbuffer_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * engine::frames_in_flight * 2);
    engine::visbuffer::init(visbuffer_barriers);
    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
        visbuffer_barriers[i].srcAccessMask = 0;
        visbuffer_barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        visbuffer_barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        visbuffer_barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    bool visbuffer_barriers_applied = false;

    uint32_t mesh_fragment_spv_size;
    auto mesh_fragment_spv_data = engine::util::read_file("mesh_fragment.spv", &mesh_fragment_spv_size);
    auto mesh_fragment_module = engine::create_shader({mesh_fragment_spv_data, mesh_fragment_spv_size});
    free(mesh_fragment_spv_data);

    uint32_t flatten_draw_spv_size;
    auto flatten_draw_spv_data = engine::util::read_file("flatten_draw.spv", &flatten_draw_spv_size);
    auto flatten_draw_module = engine::create_shader({flatten_draw_spv_data, flatten_draw_spv_size});
    free(flatten_draw_spv_data);

    auto flatten_draw_pipeline = engine::ComputePipeline(
        engine::ComputePipelineBuilder{}.shader(flatten_draw_module).push_constant(FlattenDrawPC::size));

    uint32_t culling_spv_size;
    auto culling_spv_data = engine::util::read_file("culling.spv", &culling_spv_size);
    auto culling_module = engine::create_shader({culling_spv_data, culling_spv_size});
    free(culling_spv_data);

    auto culling_pipeline =
        engine::ComputePipeline(engine::ComputePipelineBuilder{}.shader(culling_module).push_constant(CullingPC::size));

    uint32_t visbuffer_raster_vertex_spv_size;
    auto visbuffer_raster_vertex_spv_data =
        engine::util::read_file("visbuffer_raster_vertex.spv", &visbuffer_raster_vertex_spv_size);
    auto visbuffer_raster_vertex_module =
        engine::create_shader({visbuffer_raster_vertex_spv_data, visbuffer_raster_vertex_spv_size});
    free(visbuffer_raster_vertex_spv_data);

    uint32_t visbuffer_raster_fragment_spv_size;
    auto visbuffer_raster_fragment_spv_data =
        engine::util::read_file("visbuffer_raster_fragment.spv", &visbuffer_raster_fragment_spv_size);
    auto visbuffer_raster_fragment_module =
        engine::create_shader({visbuffer_raster_fragment_spv_data, visbuffer_raster_fragment_spv_size});
    free(visbuffer_raster_fragment_spv_data);

    auto visbuffer_raster_pipeline = engine::GraphicsPipeline(engine::GraphicsPipelineBuilder{}
                                                                  .vertex(visbuffer_raster_vertex_module)
                                                                  .fragment(visbuffer_raster_fragment_module)
                                                                  .push_constant_size(VisbufferRasterPC::size)
                                                                  .add_color_attachment(engine::visbuffer::format)
                                                                  .depth_format(VK_FORMAT_D16_UNORM))
                                         .depth_test(true)
                                         .depth_write(true)
                                         .depth_compare_op(engine::CompareOp::Less)
                                         .cull_mode(engine::CullMode::NoCull);

    uint32_t pbr_spv_size;
    auto pbr_spv_data = engine::util::read_file("pbr.spv", &pbr_spv_size);
    auto pbr_module = engine::create_shader({pbr_spv_data, pbr_spv_size});
    free(pbr_spv_data);

    auto pbr_shading_set_layout = engine::DescriptorSet<engine::descriptor::Binding{
        .count = 1,
        .type = engine::descriptor::Binding::UBO,
        .stages = VK_SHADER_STAGE_COMPUTE_BIT,
    }>::create();
    auto pbr_pipeline =
        engine::ComputePipeline(engine::ComputePipelineBuilder{}
                                    .shader(pbr_module)
                                    .descriptor_layout(0, engine::visbuffer::shading_set_layout)
                                    .descriptor_layout(1, pbr_shading_set_layout)
                                    .descriptor_layout(2, engine::texture_registry::get_texture_pool().set_layout)
                                    .push_constant(PBRPC::size));

    uint32_t postprocessing_spv_size;
    auto postprocessing_spv_data = engine::util::read_file("postprocessing.spv", &postprocessing_spv_size);
    auto postprocessing_module = engine::create_shader({postprocessing_spv_data, postprocessing_spv_size});
    free(postprocessing_spv_data);

    auto postprocessing_set_layout = engine::DescriptorSet<engine::descriptor::Binding{
                                                               .count = 1,
                                                               .type = engine::descriptor::Binding::StorageImage,
                                                               .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                           },
                                                           engine::descriptor::Binding{
                                                               .count = 1,
                                                               .type = engine::descriptor::Binding::StorageImage,
                                                               .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                           }>::create();
    auto postprocessing_pipeline = engine::ComputePipeline(engine::ComputePipelineBuilder{}
                                                               .shader(postprocessing_module)
                                                               .descriptor_layout(0, postprocessing_set_layout)
                                                               .push_constant(PostprocessingPC::size));

    engine::GPUImage* depth_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * engine::frames_in_flight);
    VkImageView* depth_image_views = (VkImageView*)malloc(sizeof(VkImageView) * engine::frames_in_flight);
    bool depth_barriers_applied = false;
    VkImageMemoryBarrier2* depth_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * engine::frames_in_flight);

    update_depth(depth_images, depth_image_views, depth_barriers, engine::frames_in_flight);

    engine::GPUImage* target_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * engine::frames_in_flight);
    VkImageView* target_image_views = (VkImageView*)malloc(sizeof(VkImageView) * engine::frames_in_flight);
    bool target_barriers_applied = false;
    VkImageMemoryBarrier2* target_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * engine::frames_in_flight);

    update_target(target_images, target_image_views, target_barriers, engine::frames_in_flight);

    constexpr uint32_t max_draw_size = 4096;
    engine::Buffer draw_id_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        draw_id_buffers[i] =
            engine::Buffer::create(sizeof(glm::vec4) + max_draw_size * (sizeof(glm::mat4) + sizeof(glm::vec4)),
                                   VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                                   std::nullopt);
    }

    engine::Buffer indirect_draw_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        indirect_draw_buffers[i] = engine::Buffer::create(
            sizeof(VkDrawIndirectCommand) * max_draw_size,
            VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
    }

    constexpr uint32_t max_culling_queue_size = 8192;
    engine::Buffer culling_data_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        culling_data_buffers[i] =
            engine::Buffer::create(4 + max_culling_queue_size * (8 + 8 + 4),
                                   VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                                   std::nullopt);
    }

    engine::Buffer culling_queue_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        culling_queue_buffers[i] =
            engine::Buffer::create(4 + max_culling_queue_size * (4 + 4),
                                   VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                                   std::nullopt);
    }

    constexpr uint32_t max_transform_size = 1024;
    uint32_t transform_buffer_sizes[2]{};
    engine::Buffer transform_buffers[engine::frames_in_flight];
    glm::mat4* transforms[2] = {
        (glm::mat4*)malloc(sizeof(glm::mat4) * max_transform_size),
        (glm::mat4*)malloc(sizeof(glm::mat4) * max_transform_size),
    };
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        transform_buffers[i] =
            engine::Buffer::create(sizeof(glm::mat4) * 1024, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
    }

    VkBufferMemoryBarrier2 model_barrier{};
    VkBufferMemoryBarrier2 scene_indirect_buffer_barrier{};
    bool model_barrier_applied = true;
    bool scene_indirect_buffer_barrier_applied = true;

    float fov = 90.0f;
    glm::vec3 look_at{0.0f};
    engine::Camera cam{};
    cam.set_projection(engine::camera::Perspective{
        .fov = glm::radians(fov),
        .aspect_ratio = 16.0f / 10.0f,
    });
    cam.position = glm::vec3{10.0f};
    cam.look_at(look_at);
    cam.update_matrices();

    glm::vec3 res_movement{0.0f};

    bool lock_cam = false;
    glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    engine::imgui::enable(false);

    float sensitivity = 1.0f;

    glm::vec3 light_intensity{1.0f};
    glm::vec3 light_position{5.0f};

    double accum = 0;
    double last_time = glfwGetTime();

    bool done = false;
    uint64_t update_count = 0;
    while (!glfwWindowShouldClose(engine::window)) {
        double time = glfwGetTime();
        double frame_time = time - last_time;
        last_time = time;
        accum += frame_time;

        auto state = engine::event::poll();
        if (state == engine::event::Minimized) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        while (accum >= (1000.0 / 60.0) / 1000.0) {
            accum -= (1000.0 / 60.0) / 1000.0;

            cam.set_projection(engine::camera::Perspective{
                .fov = glm::radians(fov),
                .aspect_ratio = 16.0f / 10.0f,
            });

            if (engine::event::was_released(GLFW_KEY_L)) {
                lock_cam = !lock_cam;
                glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                engine::imgui::enable(lock_cam);
            }

            glm::vec3 movement{0.0f};
            if (engine::event::is_held(GLFW_KEY_W)) {
                movement.z -= 1.0f;
            }
            if (engine::event::is_held(GLFW_KEY_S)) {
                movement.z += 1.0f;
            }
            if (engine::event::is_held(GLFW_KEY_A)) {
                movement.x -= 1.0f;
            }
            if (engine::event::is_held(GLFW_KEY_D)) {
                movement.x += 1.0f;
            }
            bool moved = movement.x != 0.0f || movement.z != 0.0f;

            res_movement = glm::vec3{0.0f};
            if (moved) {
                auto forward = cam.forward();
                auto right = cam.right();
                auto normalized_movement = glm::normalize(movement);

                cam.position += -normalized_movement.z * forward + normalized_movement.x * right;
            }

            auto mouse_delta = engine::event::get_mouse_delta();
            if (!lock_cam && (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)) {
                cam.rotate(glm::radians(-mouse_delta.x), glm::radians(-mouse_delta.y));
            }

            cam.update_matrices();

            update_count++;
            engine::event::update_tick();
        }

        if (engine::prepare_frame()) {
            goto end_of_frame;
        }

        {
            engine::imgui::begin();
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Load model")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Model files", "gltf,glb,gom"},
                        };
                        auto current_path = std::filesystem::current_path();
                        nfdopendialogu8args_t args{};
                        args.filterCount = 1;
                        args.filterList = filters;
                        args.defaultPath = current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        const nfdpathset_t* paths;
                        auto res = NFD_OpenDialogMultipleU8_With(&paths, &args);
                        if (res == NFD_OKAY) {
                            nfdpathsetenum_t enumerator;
                            NFD_PathSet_GetEnum(paths, &enumerator);

                            nfdchar_t* path;
                            std::size_t i = 0;

                            auto timeline_value = engine::transport::begin();
                            while (NFD_PathSet_EnumNext(&enumerator, &path) && path) {
                                uint32_t size;
                                auto* file = engine::util::read_file(path, &size);
                                std::filesystem::path file_path{path};

                                auto extension = file_path.extension();
                                model_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
                                model_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                                Model::DataType type;
                                if (extension == ".glb") {
                                    type = Model::GLB;
                                } else if (extension == ".gltf") {
                                    type = Model::GLTF;
                                } else if (extension == ".gom") {
                                    type = Model::GOM;
                                } else {
                                    assert(false && "Wrong filetype");
                                }

                                models.emplace_back();
                                models.back().name = file_path.stem();
                                models.back().filepath = std::move(file_path);
                                models.back().timeline = timeline_value;

                                auto& model = models.back();
                                auto err =
                                    model.load(models.back().filepath.parent_path(), type, file, size, &model_barrier);
                                if (err != engine::Model::Ok) {
                                    printf("error: %d @%d in %s\n", err, __LINE__, __FILE__);
                                    assert(false);
                                }

                                free(file);
                                NFD_PathSet_FreePath(path);
                            }
                            engine::transport::end();

                            NFD_PathSet_FreeEnum(&enumerator);
                            NFD_PathSet_Free(paths);
                        }
                    }

                    if (ImGui::MenuItem("Open scene")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Scene file", "gos"},
                        };

                        auto current_path = std::filesystem::current_path();
                        nfdopendialogu8args_t args{};
                        args.filterCount = 1;
                        args.filterList = filters;
                        args.defaultPath = current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        nfdu8char_t* path;
                        auto res = NFD_OpenDialogU8_With(&path, &args);
                        if (res == NFD_OKAY) {
                            scenes.emplace_back();

                            model_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
                            model_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                            scene_indirect_buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                            scene_indirect_buffer_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                            scenes.back().load(path, &scene_indirect_buffer_barrier, &model_barrier);
                            std::filesystem::path path_fs = path;
                            scenes.back().name = path_fs.stem();
                            scenes.back().filepath = std::move(path_fs);

                            model_barrier_applied = false;
                            scene_indirect_buffer_barrier_applied = false;

                            NFD_FreePath(path);
                        }
                    }

                    if (ImGui::MenuItem("Save scene")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Scene file", "gos"},
                        };

                        auto current_path = std::filesystem::current_path();
                        nfdsavedialogu8args_t args{};
                        args.defaultName = "scene.gos";
                        args.filterCount = 1;
                        args.filterList = filters;
                        args.defaultPath = current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        nfdu8char_t* path;
                        auto res = NFD_SaveDialogU8_With(&path, &args);
                        if (res == NFD_OKAY) {
                            uint32_t data_size;
                            auto data = serialize_scene(models.data(), models.size(), &data_size);
                            engine::util::save_file(path, (uint8_t*)data, data_size);

                            NFD_FreePath(path);
                            free(data);
                        }
                    }

                    ImGui::EndMenu();
                }
            }
            ImGui::EndMainMenuBar();

            if (ImGui::Begin("Models")) {
                uint32_t transforms_ix = 0;
                std::erase_if(models, [&](auto& model) {
                    ImGui::PushID(model.gpu_group.data.address());

                    bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                ImGuiTreeNodeFlags_SpanLabelWidth);

                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    ImGui::InputText("##name", &model.name);

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    if (ImGui::Button("Unload")) {
                        models_to_destroy[(engine::get_current_frame() - 1) % engine::frames_in_flight].emplace_back(
                            model);

                        if (open) ImGui::TreePop();
                        ImGui::PopID();

                        return true;
                    }

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    ImGui::Checkbox("Embed", &model.embed_optimized);

                    if (open) {
                        ImGui::Text("file path: %s", model.filepath.c_str());

                        if (ImGui::Button("add instance")) {
                            model.instances.emplace_back();
                            model.instances.back().name = std::format("Instance #{}", model.last_instance_id++);
                        }

                        model.instance_transforms.clear();
                        model.instance_transforms.resize(model.instances.size());
                        std::size_t i = 0;
                        std::size_t instance_counter = 0;
                        std::erase_if(model.instances, [&](auto& instance) {
                            ImGui::PushID(i);
                            auto remove =
                                imgui_model_instance(model.instances[i], model.instance_transforms[instance_counter]);
                            ImGui::PopID();

                            if (!remove) instance_counter++;

                            i++;
                            return remove;
                        });

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                    return false;
                });
            }
            ImGui::End();

            if (ImGui::Begin("Scenes")) {
                std::erase_if(scenes, [&](auto& scene) {
                    ImGui::PushID(scene.gpu_group.data.address());

                    bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                ImGuiTreeNodeFlags_SpanLabelWidth);

                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    ImGui::InputText("##name", &scene.name);

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    if (ImGui::Button("Unload")) {
                        scene.destroy();

                        if (open) ImGui::TreePop();
                        ImGui::PopID();
                        return true;
                    }

                    if (open) {
                        ImGui::DragFloat3("XYZ", glm::value_ptr(scene.translate), 0.1f, 0.0f, 0.0f, "%.2f");

                        ImGui::DragFloat("yaw", &scene.rotate.x, 0.1f, 0.0, 0.0, "%.2f");
                        ImGui::DragFloat("pitch", &scene.rotate.y, 0.1f, 0.0, 0.0, "%.2f");
                        ImGui::DragFloat("roll", &scene.rotate.z, 0.1f, 0.0, 0.0, "%.2f");

                        ImGui::DragFloat3("scale", glm::value_ptr(scene.scale), 0.1f, 0.0f, 0.0f, "%.2f");

                        scene.transform = glm::translate(glm::identity<glm::mat4>(), scene.translate) *
                                          glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(),
                                                                              scene.rotate.x, glm::vec3{0, 1, 0}),
                                                                  scene.rotate.y, glm::vec3{1, 0, 0}),
                                                      scene.rotate.z, glm::vec3{0, 0, 1}) *
                                          glm::scale(glm::identity<glm::mat4>(), scene.scale);

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                    return false;
                });
            }
            ImGui::End();

            if (ImGui::Begin("Window")) {
                ImGui::SeparatorText("Camera");
                ImGui::SliderFloat("sensitivity", &sensitivity, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("fov", &fov, 0.0f, 360.0f, "%.0f");
                ImGui::DragFloat3("position", glm::value_ptr(cam.position), 0.1f);
                ImGui::DragFloat3("look at", glm::value_ptr(look_at), 0.1f);
                if (ImGui::Button("update look at")) {
                    cam.look_at(look_at);
                    cam.update_matrices();
                }
                ImGui::Text("lock cam: %b", lock_cam);

                ImGui::SeparatorText("Lighting");
                ImGui::DragFloat3("intensity", glm::value_ptr(light_intensity), 0.1f);
                ImGui::DragFloat3("position##sybau", glm::value_ptr(light_position), 0.1f);
            }

            ImGui::End();
            engine::imgui::end();

            engine::prepare_draw();

            if (!visbuffer_barriers_applied || !depth_barriers_applied || !target_barriers_applied ||
                !model_barrier_applied || !scene_indirect_buffer_barrier_applied) {
                engine::synchronization::begin_barriers();
                if (!visbuffer_barriers_applied) {
                    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(visbuffer_barriers[i]);
                    }

                    visbuffer_barriers_applied = true;
                }
                if (!depth_barriers_applied) {
                    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(depth_barriers[i]);
                    }

                    depth_barriers_applied = true;
                }
                if (!target_barriers_applied) {
                    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(target_barriers[i]);
                    }

                    depth_barriers_applied = true;
                }
                if (!model_barrier_applied) {
                    engine::synchronization::apply_barrier(model_barrier);

                    model_barrier_applied = true;
                }
                if (!scene_indirect_buffer_barrier_applied) {
                    engine::synchronization::apply_barrier(scene_indirect_buffer_barrier);

                    scene_indirect_buffer_barrier_applied = true;
                }
                engine::synchronization::end_barriers();
            }

            uint32_t transforms_offset = 0;
            for (const auto& model : models) {
                std::memcpy(transforms[engine::get_current_frame()] + transforms_offset, model.instance_transforms.data(), model.instance_transforms.size() * sizeof(glm::mat4));
                transforms_offset += model.instance_transforms.size();
            }

            auto& transform_buffer = transform_buffers[engine::get_current_frame()];
            VkBufferMemoryBarrier2 transform_barrier{};
            transform_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            transform_barrier.pNext = nullptr;
            transform_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            transform_barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

            engine::transport::begin();
            engine::transport::upload(&transform_barrier, transforms[engine::get_current_frame()], sizeof(glm::mat4), transform_buffer.data(), 0);
            auto timeline_wait = engine::transport::end();

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(transform_barrier);
            engine::synchronization::end_barriers();
            transform_barrier.offset = 0;
            transform_barrier.size = transform_buffer.size();
            transform_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            transform_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            while (!engine::transport::is_ready(timeline_wait)) {}

            VkClearColorValue clear_color{};
            clear_color.float32[0] = 0.0f;
            clear_color.float32[1] = 0.0f;
            clear_color.float32[2] = 0.3f;
            clear_color.float32[3] = 1.0f;

            VkImageSubresourceRange clear_range{};
            clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear_range.baseMipLevel = 0;
            clear_range.layerCount = 1;
            clear_range.baseArrayLayer = 0;
            clear_range.levelCount = 1;

            vkCmdClearColorImage(engine::get_cmd_buf(), target_images[engine::get_current_frame()].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &clear_range);

            auto& culling_data_buffer = culling_data_buffers[engine::get_current_frame()];
            auto& culling_queue_buffer = culling_queue_buffers[engine::get_current_frame()];

            VkBufferMemoryBarrier2 culling_data_barrier{};
            culling_data_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            culling_data_barrier.pNext = nullptr;
            culling_data_barrier.buffer = culling_data_buffer;
            culling_data_barrier.offset = 0;
            culling_data_barrier.size = culling_data_buffer.size();
            culling_data_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            culling_data_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            culling_data_barrier.srcAccessMask = 0;
            culling_data_barrier.srcStageMask = 0;
            culling_data_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            culling_data_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            VkBufferMemoryBarrier2 culling_queue_barrier{};
            culling_queue_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            culling_queue_barrier.pNext = nullptr;
            culling_queue_barrier.buffer = culling_queue_buffer;
            culling_queue_barrier.offset = 0;
            culling_queue_barrier.size = culling_queue_buffer.size();
            culling_queue_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            culling_queue_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            culling_queue_barrier.srcAccessMask = 0;
            culling_queue_barrier.srcStageMask = 0;
            culling_queue_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            culling_queue_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            uint8_t flatten_draw_pc[FlattenDrawPC::size]{};
            flatten_draw_pipeline.bind();
            uint transform_ix = 0;
            for (auto& model : models) {
                if (!engine::transport::is_ready(model.timeline)) continue;

                for (const auto& _ : model.instance_transforms) {
                    FlattenDrawPC::write(flatten_draw_pc, model.gpu_group.data.address(),
                                         model.indirect_draw_buffer.address(), culling_data_buffer.address(),
                                         culling_queue_buffer.address(), transform_buffer.address(), transform_ix * sizeof(glm::mat4),
                                         model.gpu_data.mesh_count, max_culling_queue_size);

                    flatten_draw_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
                        .push_constant = flatten_draw_pc,
                        .group_count_x = (uint32_t)std::ceil(model.gpu_data.mesh_count / 64.0f),
                        .group_count_y = 1,
                        .group_count_z = 1,
                    });

                    transform_ix++;
                }
            }

            for (auto& scene : scenes) {
                if (!engine::transport::is_ready(scene.timeline)) continue;

                uint8_t flatten_draw_pc[FlattenDrawPC::size];
                FlattenDrawPC::write(flatten_draw_pc, scene.gpu_group.data.address(),
                                     scene.gpu_data.draw_indirect.address(), culling_data_buffer.address(),
                                     culling_queue_buffer.address(), scene.gpu_group.data.address(), 0,
                                     scene.gpu_data.draw_count, max_culling_queue_size);

                flatten_draw_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
                    .push_constant = flatten_draw_pc,
                    .group_count_x = (uint32_t)std::ceil(scene.gpu_data.draw_count / 64.0f),
                    .group_count_y = 1,
                    .group_count_z = 1,
                });
            }

            culling_data_barrier.srcAccessMask = culling_data_barrier.dstAccessMask;
            culling_data_barrier.srcStageMask = culling_data_barrier.dstStageMask;
            culling_data_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            culling_data_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            culling_queue_barrier.srcAccessMask = culling_queue_barrier.dstAccessMask;
            culling_queue_barrier.srcStageMask = culling_queue_barrier.dstStageMask;
            culling_queue_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            culling_queue_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(culling_data_barrier);
            engine::synchronization::apply_barrier(culling_queue_barrier);
            engine::synchronization::end_barriers();

            auto& draw_id_buffer = draw_id_buffers[engine::get_current_frame()];
            auto& indirect_draw_buffer = indirect_draw_buffers[engine::get_current_frame()];

            VkBufferMemoryBarrier2 draw_id_barrier{};
            draw_id_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            draw_id_barrier.pNext = nullptr;
            draw_id_barrier.buffer = draw_id_buffer;
            draw_id_barrier.offset = 0;
            draw_id_barrier.size = draw_id_buffer.size();
            draw_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            draw_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            draw_id_barrier.srcAccessMask = 0;
            draw_id_barrier.srcStageMask = 0;
            draw_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            draw_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            VkBufferMemoryBarrier2 indirect_draw_barrier{};
            indirect_draw_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            indirect_draw_barrier.pNext = nullptr;
            indirect_draw_barrier.buffer = indirect_draw_buffer;
            indirect_draw_barrier.offset = 0;
            indirect_draw_barrier.size = indirect_draw_buffer.size();
            indirect_draw_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            indirect_draw_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            indirect_draw_barrier.srcAccessMask = 0;
            indirect_draw_barrier.srcStageMask = 0;
            indirect_draw_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            indirect_draw_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            uint8_t culling_pc[CullingPC::size]{};
            CullingPC::write(culling_pc, culling_data_buffer.address(), culling_queue_buffer.address(),
                             indirect_draw_buffer.address(), draw_id_buffer.address(), max_draw_size);

            culling_pipeline.bind();
            culling_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
                .push_constant = culling_pc,
                .group_count_x = (uint32_t)std::ceil(max_culling_queue_size / 32.0f),
                .group_count_y = 1,
                .group_count_z = 1,
            });

            draw_id_barrier.srcAccessMask = draw_id_barrier.dstAccessMask;
            draw_id_barrier.srcStageMask = draw_id_barrier.dstStageMask;
            draw_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            draw_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

            indirect_draw_barrier.srcAccessMask = indirect_draw_barrier.dstAccessMask;
            indirect_draw_barrier.srcStageMask = indirect_draw_barrier.dstStageMask;
            indirect_draw_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            indirect_draw_barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(draw_id_barrier);
            engine::synchronization::apply_barrier(indirect_draw_barrier);
            engine::synchronization::end_barriers();

            engine::visbuffer::prepare_for_draw();

            engine::rendering::begin(
                engine::RenderPass{}
                    .add_color_attachment(engine::visbuffer::attach())
                    .depth_attachment(engine::RenderingAttachement{}
                                          .set_image(depth_image_views[engine::get_current_frame()],
                                                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                          .set_clear_depth(1.0f)
                                          .set_load_op(engine::LoadOp::Clear)
                                          .set_store_op(engine::StoreOp::Store)));

            uint8_t visbuffer_raster_pc[VisbufferRasterPC::size];
            VisbufferRasterPC::write(visbuffer_raster_pc, draw_id_buffer.address(),
                                     cam.view_projection());

            visbuffer_raster_pipeline.bind();
            visbuffer_raster_pipeline.draw_indirect_count(engine::GraphicsPipeline::DrawIndirectCountParams{
                .push_constant = visbuffer_raster_pc,
                .draw_buffer = indirect_draw_buffer.data(),
                .count_buffer = draw_id_buffer.data(),
                .max_draw_count = max_draw_size,
                .stride = sizeof(VkDrawIndirectCommand),
            });
            engine::rendering::end();

            engine::visbuffer::count_materials(draw_id_buffer.address());
            engine::visbuffer::get_offsets();
            engine::visbuffer::write_fragment_ids(draw_id_buffer.address());

            VkImageMemoryBarrier2 target_barrier{};
            target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            target_barrier.pNext = nullptr;
            target_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            target_barrier.image = target_images[engine::get_current_frame()].image;
            target_barrier.subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            target_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            auto shading = engine::visbuffer::shade(target_image_views[engine::get_current_frame()]);
            for (uint16_t mat_id = 0; mat_id < shading.material_id_count; mat_id++) {
                uint8_t pbr_pc[PBRPC::size]{};
                PBRPC::write(pbr_pc,
                             glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.width},
                             engine::visbuffer::stages.address() + shading.indirect_buffer_offset,
                             engine::visbuffer::stages.address() + shading.fragment_id_buffer_offset,
                             draw_id_buffer.address(), mat_id);

                PBRShadingSet shading_set_data{
                    .cam_pos = cam.position,
                    .light_pos = light_position,
                    .light_intensity = light_intensity,
                    .view_proj_matrix = cam.view_projection(),
                };

                auto pbr_shading_set = engine::descriptor::new_set(pbr_shading_set_layout);
                engine::descriptor::begin_update(pbr_shading_set);
                engine::descriptor::update_ubo(0, {(uint8_t*)&shading_set_data, sizeof(PBRShadingSet)});
                engine::descriptor::end_update();

                pbr_pipeline.bind();
                pbr_pipeline.dispatch_indirect(engine::ComputePipeline::IndirectDispatchParams{
                    .push_constant = pbr_pc,
                    .descriptor_indexes =
                        {
                            shading.vis_and_target_set,
                            pbr_shading_set,
                            engine::texture_registry::get_texture_pool().set,
                            engine::descriptor::null_set,
                        },
                    .indirect_buffer = engine::visbuffer::stages,
                    .buffer_offset = shading.indirect_buffer_offset,
                });
            }

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            auto pp_set = engine::descriptor::new_set(postprocessing_set_layout);
            engine::descriptor::begin_update(pp_set);
            engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL,
                                                     target_image_views[engine::get_current_frame()]);
            engine::descriptor::update_storage_image(1, VK_IMAGE_LAYOUT_GENERAL, engine::visbuffer::get_view());
            engine::descriptor::end_update();

            uint8_t postprocessing_push_constant[PostprocessingPC::size]{};
            PostprocessingPC::write(
                postprocessing_push_constant,
                glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height},
                models.size() > 0 ? models[0].gpu_group.data.address() : -1,
                models.size() > 0 ? models[0].gpu_group.data.address() + sizeof(engine::model::GPUMeshData) : -1);

            postprocessing_pipeline.bind();
            postprocessing_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
                .push_constant = postprocessing_push_constant,
                .descriptor_indexes =
                    {
                        pp_set,
                        engine::descriptor::null_set,
                        engine::descriptor::null_set,
                        engine::descriptor::null_set,
                    },
                .group_count_x = (uint32_t)std::ceil(engine::swapchain_extent.width / 16.0f),
                .group_count_y = (uint32_t)std::ceil(engine::swapchain_extent.height / 16.0f),
                .group_count_z = 1,
            });

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            VkImageMemoryBarrier2 swapchain_barrier{};
            swapchain_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            swapchain_barrier.pNext = nullptr;
            swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            swapchain_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchain_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchain_barrier.image = engine::get_swapchain();
            swapchain_barrier.subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            swapchain_barrier.srcAccessMask = VK_ACCESS_2_NONE;
            swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            swapchain_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::apply_barrier(swapchain_barrier);
            engine::synchronization::end_barriers();

            VkImageBlit2 blit_region{};
            blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blit_region.srcOffsets[0] = VkOffset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            };
            blit_region.srcOffsets[1] = VkOffset3D{
                .x = (int32_t)engine::swapchain_extent.width,
                .y = (int32_t)engine::swapchain_extent.height,
                .z = 1,
            };
            blit_region.srcSubresource = VkImageSubresourceLayers{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            blit_region.dstOffsets[0] = VkOffset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            };
            blit_region.dstOffsets[1] = VkOffset3D{
                .x = (int32_t)engine::swapchain_extent.width,
                .y = (int32_t)engine::swapchain_extent.height,
                .z = 1,
            };
            blit_region.dstSubresource = VkImageSubresourceLayers{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            VkBlitImageInfo2 blit_info{};
            blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blit_info.srcImage = target_images[engine::get_current_frame()].image;
            blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blit_info.dstImage = engine::get_swapchain();
            blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blit_info.filter = VK_FILTER_NEAREST;
            blit_info.regionCount = 1;
            blit_info.pRegions = &blit_region;
            vkCmdBlitImage2(engine::get_cmd_buf(), &blit_info);

            swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            swapchain_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            swapchain_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(swapchain_barrier);
            engine::synchronization::end_barriers();

            engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
                engine::RenderingAttachement{}
                    .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                    .set_load_op(engine::LoadOp::Load)
                    .set_store_op(engine::StoreOp::Store)));
            engine::imgui::render();
            engine::rendering::end();

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            transform_barrier.srcAccessMask = transform_barrier.dstAccessMask;
            transform_barrier.srcStageMask = transform_barrier.dstStageMask;
            transform_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            transform_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            culling_data_barrier.srcAccessMask = culling_data_barrier.dstAccessMask;
            culling_data_barrier.srcStageMask = culling_data_barrier.dstStageMask;
            culling_data_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            culling_data_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            culling_queue_barrier.srcAccessMask = culling_queue_barrier.dstAccessMask;
            culling_queue_barrier.srcStageMask = culling_queue_barrier.dstStageMask;
            culling_queue_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            culling_queue_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            draw_id_barrier.srcAccessMask = draw_id_barrier.dstAccessMask;
            draw_id_barrier.srcStageMask = draw_id_barrier.dstStageMask;
            draw_id_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            draw_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            indirect_draw_barrier.srcAccessMask = indirect_draw_barrier.dstAccessMask;
            indirect_draw_barrier.srcStageMask = indirect_draw_barrier.dstStageMask;
            indirect_draw_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            indirect_draw_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(transform_barrier);
            engine::synchronization::apply_barrier(culling_data_barrier);
            engine::synchronization::apply_barrier(culling_queue_barrier);
            engine::synchronization::apply_barrier(draw_id_barrier);
            engine::synchronization::apply_barrier(indirect_draw_barrier);
            engine::synchronization::end_barriers();

            engine::visbuffer::clear_buffers();
            vkCmdFillBuffer(engine::get_cmd_buf(), transform_buffer, 0, transform_buffer.size(), 0);
            vkCmdFillBuffer(engine::get_cmd_buf(), culling_data_buffer, 0, culling_data_buffer.size(), 0);
            vkCmdFillBuffer(engine::get_cmd_buf(), culling_queue_buffer, 0, culling_queue_buffer.size(), 0);
            vkCmdFillBuffer(engine::get_cmd_buf(), draw_id_buffer, 0, draw_id_buffer.size(), 0);
            vkCmdFillBuffer(engine::get_cmd_buf(), indirect_draw_buffer, 0, indirect_draw_buffer.size(), 0);

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();
        }

    end_of_frame:
        for (auto& model : models_to_destroy[engine::get_current_frame()]) {
            model.destroy();
        }
        models_to_destroy[engine::get_current_frame()].clear();

        if (engine::next_frame()) {
            for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                engine::GPUImageView::destroy(depth_image_views[i]);
                depth_images[i].destroy();

                engine::GPUImageView::destroy(target_image_views[i]);
                target_images[i].destroy();
            }

            update_depth(depth_images, depth_image_views, depth_barriers, engine::frames_in_flight);
            depth_barriers_applied = false;

            update_target(target_images, target_image_views, target_barriers, engine::frames_in_flight);
            target_barriers_applied = false;

            visbuffer_raster_pipeline.update_viewport_to_swapchain();
            visbuffer_raster_pipeline.update_scissor_to_viewport();

            engine::visbuffer::resize(visbuffer_barriers, true, false);
            for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                visbuffer_barriers[i].srcAccessMask = 0;
                visbuffer_barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                visbuffer_barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                visbuffer_barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
            visbuffer_barriers_applied = false;
        }
    }

    vkDeviceWaitIdle(engine::device);

    NFD_Quit();

    for (auto& model : models) {
        model.destroy();
    }
    for (auto& models : models_to_destroy) {
        for (auto& model : models) {
            model.destroy();
        }
    }

    for (auto& scene : scenes) {
        scene.destroy();
    }

    flatten_draw_pipeline.destroy();
    engine::destroy_shader(flatten_draw_module);

    culling_pipeline.destroy();
    engine::destroy_shader(culling_module);

    visbuffer_raster_pipeline.destroy();
    engine::destroy_shader(visbuffer_raster_fragment_module);
    engine::destroy_shader(visbuffer_raster_vertex_module);

    engine::destroy_shader(mesh_fragment_module);

    engine::destroy_descriptor_set_layout(pbr_shading_set_layout);
    pbr_pipeline.destroy();
    engine::destroy_shader(pbr_module);

    engine::destroy_descriptor_set_layout(postprocessing_set_layout);
    postprocessing_pipeline.destroy();
    engine::destroy_shader(postprocessing_module);

    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
        engine::GPUImageView::destroy(depth_image_views[i]);
        depth_images[i].destroy();

        engine::GPUImageView::destroy(target_image_views[i]);

        target_images[i].destroy();

        draw_id_buffers[i].destroy();
        indirect_draw_buffers[i].destroy();
        culling_data_buffers[i].destroy();
        culling_queue_buffers[i].destroy();
        transform_buffers[i].destroy();
        free(transforms[i]);
    }

    free(target_images);
    free(target_image_views);
    free(target_barriers);

    free(depth_image_views);
    free(depth_images);

    engine::visbuffer::destroy();
    engine::destroy();
    return 0;
}
