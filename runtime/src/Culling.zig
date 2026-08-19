const std = @import("std");
const base = @import("base");
const zm = base.zmath;
const shaders = @import("shaders");

const Self = @This();

pub const RenderableEntry = extern struct {
    transform: zm.Mat,
    geometry: u64,
    material_schema: u32,
    material_instance: u32,
};

pub const world_instance_size = @sizeOf(u32) + 16 * @sizeOf(f32);

pub const CullPC = struct {
    vp: zm.Mat,
    world_instances_address: u64,
    mesh_descs_address: u64,
    lod_entries_address: u64,
    renderables_address: u64,
    draw_cmds_address: u64,
    instance_count: u32,
    max_draw_count: u32,
    screen_width: f32,
    screen_height: f32,
    fov_y: f32,
    camera_pos: [3]f32,

    pub const size = base.push_constant.size(CullPC, base.layout.scalar);
};

pub const skinned_world_instance_size = @sizeOf(u32) + @sizeOf(u32) + 16 * @sizeOf(f32);

pub const AnimatedCullPC = struct {
    vp: zm.Mat,
    world_instances_address: u64,
    mesh_descs_address: u64,
    lod_entries_address: u64,
    renderables_address: u64,
    draw_cmds_address: u64,
    arena_address: u64,
    joints_address: u64,
    arena_capacity: u32,
    instance_count: u32,
    max_draw_count: u32,
    screen_width: f32,
    screen_height: f32,
    fov_y: f32,
    camera_pos: [3]f32,

    pub const size = base.push_constant.size(AnimatedCullPC, base.layout.scalar);
};

pipeline: base.ComputePipeline,
animated_pipeline: base.ComputePipeline,

pub fn init(ctx: base.Ctx.Query(&.{ .device, .render_extent })) !Self {
    const cull_comp = shaders.get(.cull_renderables);
    const cull_comp_mod = try base.ShaderModule.init(.from(ctx), cull_comp);
    defer cull_comp_mod.deinit(.from(ctx));

    const pipeline = try base.ComputePipeline.init(.from(ctx), .{
        .shader = cull_comp_mod,
        .entry_point = "compute",
        .push_constant_size = @intCast(base.push_constant.size(CullPC, base.layout.scalar)),
    });

    const anim_comp = shaders.get(.cull_animated_renderables);
    const anim_comp_mod = try base.ShaderModule.init(.from(ctx), anim_comp);
    defer anim_comp_mod.deinit(.from(ctx));

    const animated_pipeline = try base.ComputePipeline.init(.from(ctx), .{
        .shader = anim_comp_mod,
        .entry_point = "compute",
        .push_constant_size = @intCast(base.push_constant.size(AnimatedCullPC, base.layout.scalar)),
    });

    return .{
        .pipeline = pipeline,
        .animated_pipeline = animated_pipeline,
    };
}

pub fn deinit(self: *Self, ctx: base.Ctx.Query(&.{ .device })) void {
    self.animated_pipeline.deinit(.from(ctx));
    self.pipeline.deinit(.from(ctx));
}

pub const CullParams = struct {
    vp: zm.Mat,
    world_instances_ref: base.RenderGraph.BufferRef,
    mesh_descs_ref: base.RenderGraph.BufferRef,
    lod_entries_ref: base.RenderGraph.BufferRef,
    renderables_ref: base.RenderGraph.BufferRef,
    draw_cmds_ref: base.RenderGraph.BufferRef,
    instance_count: u32,
    max_draw_count: u32,
    screen_width: f32,
    screen_height: f32,
    fov_y: f32,
    camera_pos: [3]f32,
    pc_buf: *[CullPC.size]u8,
};

pub fn cull(
    self: *Self,
    rg: *base.RenderGraph,
    params: CullParams,
) !void {
    const p = &params;
    base.push_constant.write(CullPC, p.pc_buf, .{
        .vp = p.vp,
        .world_instances_address = rg.getBuffer(p.world_instances_ref).address,
        .mesh_descs_address = rg.getBuffer(p.mesh_descs_ref).address,
        .lod_entries_address = rg.getBuffer(p.lod_entries_ref).address,
        .renderables_address = rg.getBuffer(p.renderables_ref).address,
        .draw_cmds_address = rg.getBuffer(p.draw_cmds_ref).address,
        .instance_count = p.instance_count,
        .max_draw_count = p.max_draw_count,
        .screen_width = p.screen_width,
        .screen_height = p.screen_height,
        .fov_y = p.fov_y,
        .camera_pos = p.camera_pos,
    }, base.layout.scalar);

    const compute_pass = try rg.addComputePass(.{
        .pipeline = &self.pipeline,
        .dispatch = .{
            .push_constant = p.pc_buf,
            .group_count_x = (p.instance_count + 31) / 32,
            .group_count_y = 1,
            .group_count_z = 1,
        },
    });
    try compute_pass.readBuffer(p.world_instances_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try compute_pass.readBuffer(p.mesh_descs_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try compute_pass.readBuffer(p.lod_entries_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try compute_pass.writeBuffer(p.renderables_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });
    try compute_pass.writeBuffer(p.draw_cmds_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });
}

pub const AnimatedCullParams = struct {
    vp: zm.Mat,
    world_instances_ref: base.RenderGraph.BufferRef,
    mesh_descs_ref: base.RenderGraph.BufferRef,
    lod_entries_ref: base.RenderGraph.BufferRef,
    renderables_ref: base.RenderGraph.BufferRef,
    draw_cmds_ref: base.RenderGraph.BufferRef,
    arena_ref: base.RenderGraph.BufferRef,
    joints_ref: base.RenderGraph.BufferRef,
    arena_capacity: u32,
    instance_count: u32,
    max_draw_count: u32,
    screen_width: f32,
    screen_height: f32,
    fov_y: f32,
    camera_pos: [3]f32,
    pc_buf: *[AnimatedCullPC.size]u8,
};

pub fn cullAnimated(
    self: *Self,
    rg: *base.RenderGraph,
    params: AnimatedCullParams,
) !void {
    const p = &params;
    base.push_constant.write(AnimatedCullPC, p.pc_buf, .{
        .vp = p.vp,
        .world_instances_address = rg.getBuffer(p.world_instances_ref).address,
        .mesh_descs_address = rg.getBuffer(p.mesh_descs_ref).address,
        .lod_entries_address = rg.getBuffer(p.lod_entries_ref).address,
        .renderables_address = rg.getBuffer(p.renderables_ref).address,
        .draw_cmds_address = rg.getBuffer(p.draw_cmds_ref).address,
        .arena_address = rg.getBuffer(p.arena_ref).address,
        .joints_address = rg.getBuffer(p.joints_ref).address,
        .arena_capacity = p.arena_capacity,
        .instance_count = p.instance_count,
        .max_draw_count = p.max_draw_count,
        .screen_width = p.screen_width,
        .screen_height = p.screen_height,
        .fov_y = p.fov_y,
        .camera_pos = p.camera_pos,
    }, base.layout.scalar);

    const compute_pass = try rg.addComputePass(.{
        .pipeline = &self.animated_pipeline,
        .dispatch = .{
            .push_constant = p.pc_buf,
            .group_count_x = (p.instance_count + 31) / 32,
            .group_count_y = 1,
            .group_count_z = 1,
        },
    });
    try compute_pass.readBuffer(p.world_instances_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try compute_pass.readBuffer(p.mesh_descs_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try compute_pass.readBuffer(p.lod_entries_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try compute_pass.writeBuffer(p.renderables_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });
    try compute_pass.writeBuffer(p.draw_cmds_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });
    try compute_pass.readBuffer(p.arena_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true, .shader_storage_read_bit = true },
    });
    try compute_pass.readBuffer(p.joints_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
}
