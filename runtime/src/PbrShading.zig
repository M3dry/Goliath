const std = @import("std");
const base = @import("base");
const shaders = @import("shaders");
const zmesh = @import("zmesh");
const Texture = @import("Texture.zig");
const Visbuffer = @import("Visbuffer.zig");

const zcgltf = zmesh.io.zcgltf;

const Self = @This();

const AssetGid = @import("AssetSystem.zig").Gid;

pipeline: base.ComputePipeline,
ubo_set_layout: base.vk.DescriptorSetLayout,

// glTF texture index written for a slot with no texture; the loader remaps it to
// the pool's fallback texture before upload.
pub const no_texture: u32 = std.math.maxInt(u32);

pub const schema_id: u16 = 0;

pub const PBRInstance = extern struct {
    albedo_map: AssetGid,
    metallic_roughness_map: AssetGid,
    normal_map: AssetGid,
    occlusion_map: AssetGid,
    emissive_map: AssetGid,

    albedo_texcoord: u32,
    metallic_roughness_texcoord: u32,
    normal_texcoord: u32,
    occlusion_texcoord: u32,
    emissive_texcoord: u32,

    albedo: [4]f32,
    metallic_factor: f32,
    roughness_factor: f32,
    normal_factor: f32,
    occlusion_factor: f32,
    emissive_factor: [3]f32,

    pub const texture_offsets: []const usize = &.{
        @offsetOf(PBRInstance, "albedo_map"),
        @offsetOf(PBRInstance, "metallic_roughness_map"),
        @offsetOf(PBRInstance, "normal_map"),
        @offsetOf(PBRInstance, "occlusion_map"),
        @offsetOf(PBRInstance, "emissive_map"),
    };
};

pub const GltfPBRInstance = struct {
    albedo_map: u32,
    metallic_roughness_map: u32,
    normal_map: u32,
    occlusion_map: u32,
    emissive_map: u32,

    albedo_texcoord: u32,
    metallic_roughness_texcoord: u32,
    normal_texcoord: u32,
    occlusion_texcoord: u32,
    emissive_texcoord: u32,

    albedo: [4]f32,
    metallic_factor: f32,
    roughness_factor: f32,
    normal_factor: f32,
    occlusion_factor: f32,
    emissive_factor: [3]f32,

    /// Texture slots carry glTF texture indices (or `no_texture`); the loader
    /// remaps them to texture-pool indices before the instance buffer is uploaded.
    pub fn fromGltf(data: *zcgltf.Data, material_index: u32) !GltfPBRInstance {
        const materials = data.materials orelse return error.NoMaterials;
        if (material_index >= data.materials_count) return error.InvalidMaterialIndex;
        const mat = &materials[material_index];

        var inst = GltfPBRInstance{
            .albedo_map = no_texture,
            .metallic_roughness_map = no_texture,
            .normal_map = no_texture,
            .occlusion_map = no_texture,
            .emissive_map = no_texture,
            .albedo_texcoord = 0,
            .metallic_roughness_texcoord = 0,
            .normal_texcoord = 0,
            .occlusion_texcoord = 0,
            .emissive_texcoord = 0,
            .albedo = .{ 1, 1, 1, 1 },
            .metallic_factor = 1,
            .roughness_factor = 1,
            .normal_factor = 1,
            .occlusion_factor = 1,
            .emissive_factor = .{ 0, 0, 0 },
        };

        if (mat.has_pbr_metallic_roughness != 0) {
            const pbr = mat.pbr_metallic_roughness;
            inst.albedo = pbr.base_color_factor;
            inst.metallic_factor = pbr.metallic_factor;
            inst.roughness_factor = pbr.roughness_factor;

            const bc = Texture.parseTextureView(data, pbr.base_color_texture);
            if (bc.texture_index) |ix| {
                inst.albedo_map = ix;
                inst.albedo_texcoord = bc.texcoord;
            }
            const mr = Texture.parseTextureView(data, pbr.metallic_roughness_texture);
            if (mr.texture_index) |ix| {
                inst.metallic_roughness_map = ix;
                inst.metallic_roughness_texcoord = mr.texcoord;
            }
        }

        const nt = Texture.parseTextureView(data, mat.normal_texture);
        if (nt.texture_index) |ix| {
            inst.normal_map = ix;
            inst.normal_texcoord = nt.texcoord;
            inst.normal_factor = nt.scale;
        }
        const ot = Texture.parseTextureView(data, mat.occlusion_texture);
        if (ot.texture_index) |ix| {
            inst.occlusion_map = ix;
            inst.occlusion_texcoord = ot.texcoord;
            inst.occlusion_factor = ot.scale;
        }
        const et = Texture.parseTextureView(data, mat.emissive_texture);
        if (et.texture_index) |ix| {
            inst.emissive_map = ix;
            inst.emissive_texcoord = et.texcoord;
        }
        inst.emissive_factor = mat.emissive_factor;

        return inst;
    }
};

pub const GPUPBRInstance = extern struct {
    albedo_map: u32,
    metallic_roughness_map: u32,
    normal_map: u32,
    occlusion_map: u32,
    emissive_map: u32,

    albedo_texcoord: u32,
    metallic_roughness_texcoord: u32,
    normal_texcoord: u32,
    occlusion_texcoord: u32,
    emissive_texcoord: u32,

    albedo: [4]f32,
    metallic_factor: f32,
    roughness_factor: f32,
    normal_factor: f32,
    occlusion_factor: f32,
    emissive_factor: [3]f32,
};

pub const PBRPC = struct {
    screen: [2]u32,
    dispatch_address: u64,
    frag_ids_address: u64,
    renderables_address: u64,
    instances_address: u64,
};
pub const pbr_pc_size = base.push_constant.size(PBRPC, base.layout.scalar);

pub const ShadingData = extern struct {
    cam_pos: [3]f32,
    view_proj_matrix: [16]f32,
    lights_address: u64,
    light_count: u32,
};

pub fn init(ctx: base.Ctx.Query(&.{ .device, .render_extent }), vis_set_layout: base.vk.DescriptorSetLayout, texture_pool_set_layout: base.vk.DescriptorSetLayout) !Self {
    const ubo_set_layout = try ctx.view.device.createDescriptorSetLayout(&.{
        .binding_count = 1,
        .p_bindings = &.{
            base.vk.DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptor_type = .uniform_buffer,
                .descriptor_count = 1,
                .stage_flags = .{ .compute_bit = true },
            },
        },
    }, null);
    errdefer ctx.view.device.destroyDescriptorSetLayout(ubo_set_layout, null);

    const pbr_mod = try base.ShaderModule.init(.from(ctx), shaders.get(.visbuffer_pbr_shading));
    defer pbr_mod.deinit(.from(ctx));

    const pipeline = try base.ComputePipeline.init(.from(ctx), .{
        .shader = pbr_mod,
        .entry_point = "compute",
        .set_layouts = &.{ vis_set_layout, texture_pool_set_layout, ubo_set_layout },
        .push_constant_size = @intCast(pbr_pc_size),
    });
    errdefer pipeline.deinit(.from(ctx));

    return .{
        .pipeline = pipeline,
        .ubo_set_layout = ubo_set_layout,
    };
}

pub fn deinit(self: *Self, ctx: base.Ctx.Query(&.{ .device })) void {
    self.pipeline.deinit(ctx);
    ctx.view.device.destroyDescriptorSetLayout(self.ubo_set_layout, null);
}

pub const ShadeParams = struct {
    screen: [2]u32,
    vis_ref: base.RenderGraph.ImageRef,
    target_ref: base.RenderGraph.ImageRef,
    vis_set_id: u64,
    dispatch_ref: base.RenderGraph.BufferRef,
    frag_ids_ref: base.RenderGraph.BufferRef,
    renderables_ref: base.RenderGraph.BufferRef,
    instances_ref: base.RenderGraph.BufferRef,
    arena_ref: base.RenderGraph.BufferRef,
    cam_pos: [3]f32,
    view_proj: base.zmath.Mat,
    lights_address: u64,
    light_count: u32,
    pc_buf: *[pbr_pc_size]u8,
};

/// One indirect compute dispatch per schema; each schema has its own pipeline
/// and instance buffer. 0-fragment schemas dispatch 0 groups and are no-ops.
pub fn shade(
    self: *Self,
    rg: *base.RenderGraph,
    alloc: std.mem.Allocator,
    ctx: base.Ctx.Query(&.{ .device, .descriptor_pool }),
    texture_pool_set: u64,
    params: ShadeParams,
) !void {
    const p = &params;

    var sd = ShadingData{
        .cam_pos = p.cam_pos,
        .view_proj_matrix = undefined,
        .lights_address = p.lights_address,
        .light_count = p.light_count,
    };
    @memcpy(std.mem.asBytes(&sd.view_proj_matrix), std.mem.asBytes(&p.view_proj));

    const ubo_set = try ctx.view.descriptor_pool.newSet(.from(ctx), self.ubo_set_layout);
    ctx.view.descriptor_pool.beginUpdate(ubo_set);
    try ctx.view.descriptor_pool.updateUbo(alloc, 0, std.mem.asBytes(&sd));
    ctx.view.descriptor_pool.endUpdate(.from(ctx));

    const entry_stride = @sizeOf(Visbuffer.DispatchEntry);

    base.push_constant.write(PBRPC, p.pc_buf, .{
        .screen = p.screen,
        .dispatch_address = rg.getBuffer(p.dispatch_ref).address + schema_id * entry_stride,
        .frag_ids_address = rg.getBuffer(p.frag_ids_ref).address,
        .renderables_address = rg.getBuffer(p.renderables_ref).address,
        .instances_address = rg.getBuffer(p.instances_ref).address,
    }, base.layout.scalar);

    const pass = try rg.addComputePass(.{
        .pipeline = &self.pipeline,
        .descriptor_sets = &.{ p.vis_set_id, texture_pool_set, ubo_set },
        .dispatch = .{ .group_count_x = 1, .group_count_y = 1, .group_count_z = 1 },
        .indirect = .{
            .push_constant = p.pc_buf,
            .buffer = p.dispatch_ref,
            .offset = schema_id * entry_stride,
        },
    });
    try pass.readImage(p.vis_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
        .layout = .general,
    });
    try pass.writeImage(p.target_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
        .layout = .general,
    });
    try pass.readBuffer(p.frag_ids_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try pass.readBuffer(p.renderables_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try pass.readBuffer(p.instances_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try pass.readBuffer(p.dispatch_ref, .{
        .stage = .{ .compute_shader_bit = true, .draw_indirect_bit = true },
        .access = .{ .shader_storage_read_bit = true, .indirect_command_read_bit = true },
    });
    // Skinned geometry cache, written by the skinned raster earlier this frame.
    try pass.readBuffer(p.arena_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
}
