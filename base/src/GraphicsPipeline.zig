const std = @import("std");
const vk = @import("vulkan");
const ShaderModule = @import("Shader.zig");

const Ctx = @import("root.zig").Ctx;
const GraphicsCtx = @import("GraphicsCtx.zig");

const Self = @This();

pub const ColorAttachment = struct {
    format: vk.Format,
    blend: vk.PipelineColorBlendAttachmentState = .{
        .blend_enable = .false,
        .src_color_blend_factor = .zero,
        .dst_color_blend_factor = .zero,
        .color_blend_op = .add,
        .src_alpha_blend_factor = .zero,
        .dst_alpha_blend_factor = .zero,
        .alpha_blend_op = .add,
        .color_write_mask = .{ .r_bit = true, .g_bit = true, .b_bit = true, .a_bit = true },
    },
};

pub const DrawParams = struct {
    push_constant: ?[]const u8 = null,
    vertex_count: u32,
    instance_count: u32 = 1,
    first_vertex: u32 = 0,
    first_instance: u32 = 0,
};

pub const DrawIndirectParams = struct {
    push_constant: ?[]const u8 = null,
    buffer: vk.Buffer,
    offset: u32 = 0,
    draw_count: u32,
    stride: u32 = @sizeOf(vk.DrawIndirectCommand),
};

pub const DrawIndirectCountParams = struct {
    push_constant: ?[]const u8 = null,
    buffer: vk.Buffer,
    offset: u32 = 0,
    count_buffer: vk.Buffer,
    count_offset: u32 = 0,
    max_draw_count: u32,
    stride: u32 = @sizeOf(vk.DrawIndirectCommand),
};

handle: vk.Pipeline,
layout: vk.PipelineLayout,
push_constant_size: u32,

viewport: vk.Viewport,
scissor: vk.Rect2D,
topology: vk.PrimitiveTopology = .triangle_list,
primitive_restart_enable: vk.Bool32 = .false,
cull_mode: vk.CullModeFlags = .{},
front_face: vk.FrontFace = .counter_clockwise,
line_width: f32 = 1.0,
stencil_test_enable: vk.Bool32 = .false,
stencil_face_flags: vk.StencilFaceFlags = .{ .front_bit = true, .back_bit = true },
stencil_fail_op: vk.StencilOp = .keep,
stencil_pass_op: vk.StencilOp = .keep,
stencil_depth_fail_op: vk.StencilOp = .keep,
stencil_compare_op: vk.CompareOp = .never,
stencil_compare_mask: u32 = 0,
stencil_write_mask: u32 = 0,
depth_test_enable: vk.Bool32 = .false,
depth_write_enable: vk.Bool32 = .false,
depth_compare_op: vk.CompareOp = .never,
depth_bias_enable: vk.Bool32 = .false,
depth_bias_constant_factor: f32 = 0,
depth_bias_clamp: f32 = 0,
depth_bias_slope_factor: f32 = 0,

pub const Description = struct {
    vertex: ShaderModule,
    fragment: ShaderModule,
    set_layouts: []const vk.DescriptorSetLayout = &.{},
    push_constant_size: u32 = 0,
    fill_mode: vk.PolygonMode = .fill,
    color_attachments: []const ColorAttachment = &.{},
    depth_format: vk.Format = .undefined,
    stencil_format: vk.Format = .undefined,
};

const max_attachments = 16;

pub fn init(ctx: *const Ctx, desc: Description) !Self {
    const dev = ctx.graphics.dev;
    const extent = ctx.render_extent;

    const attachment_count = desc.color_attachments.len;
    if (attachment_count > max_attachments) return error.TooManyColorAttachments;

    var color_formats: [max_attachments]vk.Format = undefined;
    var blend_attachments: [max_attachments]vk.PipelineColorBlendAttachmentState = undefined;
    for (desc.color_attachments, 0..) |ca, i| {
        color_formats[i] = ca.format;
        blend_attachments[i] = ca.blend;
    }

    const stages = [_]vk.PipelineShaderStageCreateInfo{
        desc.vertex.stageInfo(.{ .vertex_bit = true }),
        desc.fragment.stageInfo(.{ .fragment_bit = true }),
    };

    const vertex_input = vk.PipelineVertexInputStateCreateInfo{};

    const input_assembly = vk.PipelineInputAssemblyStateCreateInfo{
        .topology = .triangle_list,
        .primitive_restart_enable = .false,
    };

    const viewport_state = vk.PipelineViewportStateCreateInfo{
        .viewport_count = 1,
        .scissor_count = 1,
    };

    const rasterizer = vk.PipelineRasterizationStateCreateInfo{
        .depth_clamp_enable = .false,
        .rasterizer_discard_enable = .false,
        .polygon_mode = desc.fill_mode,
        .cull_mode = .{},
        .front_face = .counter_clockwise,
        .depth_bias_enable = .false,
        .depth_bias_constant_factor = 0,
        .depth_bias_clamp = 0,
        .depth_bias_slope_factor = 0,
        .line_width = 0,
    };

    const multisample = vk.PipelineMultisampleStateCreateInfo{
        .rasterization_samples = .{ .@"1_bit" = true },
        .sample_shading_enable = .false,
        .min_sample_shading = 0,
        .alpha_to_coverage_enable = .false,
        .alpha_to_one_enable = .false,
    };

    const depth_stencil = vk.PipelineDepthStencilStateCreateInfo{
        .depth_test_enable = .false,
        .depth_write_enable = .false,
        .depth_compare_op = .never,
        .depth_bounds_test_enable = .false,
        .stencil_test_enable = .false,
        .front = .{
            .fail_op = .keep,
            .pass_op = .keep,
            .depth_fail_op = .keep,
            .compare_op = .never,
            .compare_mask = 0,
            .write_mask = 0,
            .reference = 0,
        },
        .back = .{
            .fail_op = .keep,
            .pass_op = .keep,
            .depth_fail_op = .keep,
            .compare_op = .never,
            .compare_mask = 0,
            .write_mask = 0,
            .reference = 0,
        },
        .min_depth_bounds = 0,
        .max_depth_bounds = 0,
    };

    const blend = vk.PipelineColorBlendStateCreateInfo{
        .logic_op_enable = .false,
        .logic_op = .clear,
        .attachment_count = @intCast(attachment_count),
        .p_attachments = &blend_attachments,
        .blend_constants = .{ 0, 0, 0, 0 },
    };

    const dynamic_states = [_]vk.DynamicState{
        .viewport,
        .scissor,
        .line_width,
        .cull_mode,
        .front_face,
        .primitive_topology,
        .depth_test_enable,
        .depth_write_enable,
        .depth_compare_op,
        .depth_bias_enable,
        .depth_bias,
        .primitive_restart_enable,
        .stencil_test_enable,
        .stencil_op,
        .stencil_compare_mask,
        .stencil_write_mask,
    };

    const dynamic_state = vk.PipelineDynamicStateCreateInfo{
        .dynamic_state_count = dynamic_states.len,
        .p_dynamic_states = &dynamic_states,
    };

    var push_constant_range: vk.PushConstantRange = undefined;
    const pc_range_count: u32 = if (desc.push_constant_size > 0) 1 else 0;
    if (desc.push_constant_size > 0) {
        push_constant_range = .{
            .stage_flags = .{ .vertex_bit = true, .fragment_bit = true },
            .offset = 0,
            .size = desc.push_constant_size,
        };
    }

    const pipeline_layout = try dev.createPipelineLayout(&.{
        .set_layout_count = @intCast(desc.set_layouts.len),
        .p_set_layouts = desc.set_layouts.ptr,
        .push_constant_range_count = pc_range_count,
        .p_push_constant_ranges = if (desc.push_constant_size > 0) (&push_constant_range)[0..1] else undefined,
    }, null);
    errdefer dev.destroyPipelineLayout(pipeline_layout, null);

    const rendering = vk.PipelineRenderingCreateInfo{
        .view_mask = 0,
        .color_attachment_count = @intCast(attachment_count),
        .p_color_attachment_formats = &color_formats,
        .depth_attachment_format = desc.depth_format,
        .stencil_attachment_format = desc.stencil_format,
    };

    var pipeline: vk.Pipeline = .null_handle;
    _ = try dev.createGraphicsPipelines(.null_handle, (&vk.GraphicsPipelineCreateInfo{
        .p_next = &rendering,
        .stage_count = 2,
        .p_stages = &stages,
        .p_vertex_input_state = &vertex_input,
        .p_input_assembly_state = &input_assembly,
        .p_viewport_state = &viewport_state,
        .p_rasterization_state = &rasterizer,
        .p_multisample_state = &multisample,
        .p_depth_stencil_state = &depth_stencil,
        .p_color_blend_state = &blend,
        .p_dynamic_state = &dynamic_state,
        .layout = pipeline_layout,
        .subpass = 0,
        .base_pipeline_index = -1,
    })[0..1], null, (&pipeline)[0..1]);

    return Self{
        .handle = pipeline,
        .layout = pipeline_layout,
        .push_constant_size = desc.push_constant_size,
        .viewport = .{
            .x = 0,
            .y = 0,
            .width = @floatFromInt(extent.width),
            .height = @floatFromInt(extent.height),
            .min_depth = 0,
            .max_depth = 1,
        },
        .scissor = .{
            .offset = .{ .x = 0, .y = 0 },
            .extent = extent,
        },
    };
}

pub fn deinit(self: *Self, ctx: *const Ctx) void {
    const dev = ctx.graphics.dev;
    if (self.handle != .null_handle) {
        dev.destroyPipeline(self.handle, null);
        self.handle = .null_handle;
    }
    if (self.layout != .null_handle) {
        dev.destroyPipelineLayout(self.layout, null);
        self.layout = .null_handle;
    }
}

pub fn bind(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer) void {
    gc.dev.cmdSetPrimitiveTopology(cmd_buf, self.topology);
    gc.dev.cmdSetPrimitiveRestartEnable(cmd_buf, self.primitive_restart_enable);
    gc.dev.cmdSetViewport(cmd_buf, 0, (&self.viewport)[0..1]);
    gc.dev.cmdSetScissor(cmd_buf, 0, (&self.scissor)[0..1]);
    gc.dev.cmdSetCullMode(cmd_buf, self.cull_mode);
    gc.dev.cmdSetFrontFace(cmd_buf, self.front_face);
    gc.dev.cmdSetLineWidth(cmd_buf, self.line_width);
    gc.dev.cmdSetStencilTestEnable(cmd_buf, self.stencil_test_enable);
    if (self.stencil_test_enable == .true) {
        gc.dev.cmdSetStencilOp(cmd_buf, self.stencil_face_flags, self.stencil_fail_op, self.stencil_pass_op, self.stencil_depth_fail_op, self.stencil_compare_op);
        gc.dev.cmdSetStencilCompareMask(cmd_buf, self.stencil_face_flags, self.stencil_compare_mask);
        gc.dev.cmdSetStencilWriteMask(cmd_buf, self.stencil_face_flags, self.stencil_write_mask);
    }
    gc.dev.cmdSetDepthTestEnable(cmd_buf, self.depth_test_enable);
    if (self.depth_test_enable == .true) {
        gc.dev.cmdSetDepthCompareOp(cmd_buf, self.depth_compare_op);
    }
    gc.dev.cmdSetDepthWriteEnable(cmd_buf, self.depth_write_enable);
    gc.dev.cmdSetDepthBiasEnable(cmd_buf, self.depth_bias_enable);
    if (self.depth_bias_enable == .true) {
        gc.dev.cmdSetDepthBias(cmd_buf, self.depth_bias_constant_factor, self.depth_bias_clamp, self.depth_bias_slope_factor);
    }
    gc.dev.cmdBindPipeline(cmd_buf, .graphics, self.handle);
}

pub fn updateViewport(self: *Self, extent: vk.Extent2D) void {
    self.viewport = .{
        .x = 0,
        .y = 0,
        .width = @floatFromInt(extent.width),
        .height = @floatFromInt(extent.height),
        .min_depth = 0,
        .max_depth = 1,
    };
    self.scissor = .{
        .offset = .{ .x = 0, .y = 0 },
        .extent = extent,
    };
}

pub fn draw(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer, params: DrawParams) void {
    if (params.push_constant) |pc| {
        gc.dev.cmdPushConstants(cmd_buf, self.layout, .{ .vertex_bit = true, .fragment_bit = true }, 0, @intCast(pc.len), pc.ptr);
    }

    gc.dev.cmdDraw(cmd_buf, params.vertex_count, params.instance_count, params.first_vertex, params.first_instance);
}

pub fn drawIndirect(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer, params: DrawIndirectParams) void {
    if (params.push_constant) |pc| {
        gc.dev.cmdPushConstants(cmd_buf, self.layout, .{ .vertex_bit = true, .fragment_bit = true }, 0, @intCast(pc.len), pc.ptr);
    }

    gc.dev.cmdDrawIndirect(cmd_buf, params.buffer, params.offset, params.draw_count, params.stride);
}

pub fn drawIndirectCount(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer, params: DrawIndirectCountParams) void {
    if (params.push_constant) |pc| {
        gc.dev.cmdPushConstants(cmd_buf, self.layout, .{ .vertex_bit = true, .fragment_bit = true }, 0, @intCast(pc.len), pc.ptr);
    }
    gc.dev.cmdDrawIndirectCount(cmd_buf, params.buffer, params.offset, params.count_buffer, params.count_offset, params.max_draw_count, params.stride);
}
