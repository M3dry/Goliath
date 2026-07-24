const std = @import("std");
const runtime = @import("runtime");

const base = runtime.base;
const zgui = base.zgui;
const zm = base.zmath;

const zmesh = runtime.zmesh;

const shaders = @import("shaders");

const Culling = runtime.Culling;

fn generateCheckerboard(allocator: std.mem.Allocator, width: u32, height: u32, cell_size: u32) !base.image_loader.ImageData {
    const pixels = try allocator.alloc(u8, (width * height * 4));
    errdefer allocator.free(pixels);

    const colors = [_]u8{ 0xFF, 0xCC, 0x88, 0xFF, 0x44, 0x22, 0x11, 0xFF };
    for (0..height) |y| {
        for (0..width) |x| {
            const cx = x / cell_size;
            const cy = y / cell_size;
            const ci = @as(usize, (cx + cy) % 2) * 4;
            const i = (y * width + x) * 4;
            pixels[i + 0] = colors[ci + 0];
            pixels[i + 1] = colors[ci + 1];
            pixels[i + 2] = colors[ci + 2];
            pixels[i + 3] = colors[ci + 3];
        }
    }

    return .{
        .pixels = pixels,
        .width = width,
        .height = height,
    };
}

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const render_extent: base.vk.Extent2D = .{ .width = 1920, .height = 1080 };
    const render_format: base.vk.Format = .r32g32b32a32_sfloat;
    var ctx = try base.Ctx.init(gpa, "Demo", .{
        .name = "Demo",
        .resizable = false,
        .size = .fullscreen,
        .render_extent = render_extent,
        .blit_strategy = .letterbox,
        .render_format = render_format,
    });
    defer ctx.deinit(gpa);

    var transport: base.Transport = undefined;
    try transport.init(&ctx.graphics, gpa, init.io);
    defer transport.deinit(&ctx.graphics);

    var mh = runtime.MeshHandler.empty;
    defer mh.deinit(gpa, &ctx.destroy_queue, &transport);

    zmesh.init(gpa);
    defer zmesh.deinit();

    const gltf_data = try zmesh.io.parseAndLoadFile("Paladin.glb");
    defer zmesh.io.freeData(gltf_data);

    const skeleton = try runtime.Skeleton.fromGltf(gpa, gltf_data, 66);
    defer skeleton.deinit(gpa);

    const skin = try runtime.Skin.fromGltf(gpa, gltf_data, 0, &skeleton);
    defer skin.deinit(gpa);

    var anims: [3]runtime.Animation = undefined;
    for (&anims, 0..) |*a, i| a.* = try runtime.Animation.fromGltf(gpa, gltf_data, @intCast(i), &skeleton);
    defer for (&anims) |*a| a.deinit(gpa);

    // Find the node that references mesh 0 and grab its world transform
    // const target_mesh = &gltf_data.meshes.?[0];
    const mesh_world = zm.scaling(0.05, 0.05, 0.05);
    // const mesh_world: zm.Mat = blk: {
    //     for (gltf_data.nodes.?[0..gltf_data.nodes_count]) |*node| {
    //         if (node.mesh == target_mesh) {
    //             const w = node.transformWorld();
    //             break :blk zm.matFromArr(w);
    //         }
    //     }
    //     break :blk zm.identity();
    // };

    const gltf_result0 = try runtime.MeshIO.fromGltfPrimitive(gpa, gltf_data, 0, 0);
    defer gpa.free(gltf_result0.name);
    defer gltf_result0.mesh_io.deinit(gpa);

    const gltf_result1 = try runtime.MeshIO.fromGltfPrimitive(gpa, gltf_data, 1, 0);
    defer gpa.free(gltf_result1.name);
    defer gltf_result1.mesh_io.deinit(gpa);

    var body_mesh = try runtime.Mesh.init(gpa, gltf_result1.mesh_io.source);
    defer body_mesh.deinit(gpa);

    var helmet_mesh = try runtime.Mesh.init(gpa, gltf_result0.mesh_io.source);
    defer helmet_mesh.deinit(gpa);

    try mh.registerMesh(&body_mesh, gpa, &ctx.graphics, &transport, &ctx.destroy_queue);
    defer mh.unregisterMesh(&body_mesh, &ctx.destroy_queue, &transport);

    try mh.registerMesh(&helmet_mesh, gpa, &ctx.graphics, &transport, &ctx.destroy_queue);
    defer mh.unregisterMesh(&helmet_mesh, &ctx.destroy_queue, &transport);

    var joint_matrices_bufs: [base.Ctx.frames_in_flight]base.Buffer = undefined;
    for (&joint_matrices_bufs, 0..) |*buf, i| {
        buf.* = try base.Buffer.init(&ctx.graphics, .graphics, "Joint matrices buffer", skin.skeleton_node_indices.len * @sizeOf(zm.Mat), .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .cpu_to_gpu_dynamic);
        _ = i;
    }
    defer for (&joint_matrices_bufs) |*buf| buf.deinit(&ctx.destroy_queue);

    try mh.flushDescriptorArrays(&ctx.graphics, &ctx.destroy_queue, &transport);

    {
        const geo_buf = body_mesh.lods[0].geometry_buffer.?;
        while (!try transport.isReady(geo_buf.@"1")) {
            try transport.drain(&ctx.graphics);
            std.Thread.yield() catch {};
        }
    }
    while (!try transport.isReady(mh.ticket)) {
        try transport.drain(&ctx.graphics);
        std.Thread.yield() catch {};
    }

    var input = base.Input{};
    input.init(ctx.window);
    defer input.deinit(ctx.window);

    var imgui = try base.Imgui.init(gpa, &ctx);
    defer imgui.deinit(ctx.graphics.dev);

    const tex_width = 256;
    const tex_height = 256;
    const tex_format = base.vk.Format.r8g8b8a8_srgb;

    const tex_data = try generateCheckerboard(gpa, tex_width, tex_height, 32);
    defer gpa.free(tex_data.pixels);

    var checker_image = try base.Image2D.init(&ctx.graphics, ctx.graphics.vma_alloc, "checkerboard", .{
        .format = tex_format,
        .extent = .{ .width = tex_width, .height = tex_height },
        .usage = .{ .transfer_dst_bit = true, .sampled_bit = true },
    });
    defer checker_image.deinit(&ctx.destroy_queue);

    {
        const tick = try transport.uploadImage(
            false,
            tex_format,
            .{ .width = tex_width, .height = tex_height, .depth = 1 },
            tex_data.pixels,
            null,
            checker_image.handle,
            .{
                .aspect_mask = .{ .color_bit = true },
                .mip_level = 0,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .{ .x = 0, .y = 0, .z = 0 },
            .undefined,
            .shader_read_only_optimal,
            .{ .fragment_shader_bit = true },
            .{ .shader_read_bit = true },
        );
        while (!try transport.isReady(tick)) {
            try transport.drain(&ctx.graphics);
            std.Thread.yield() catch {};
        }
    }

    var checker_view = try base.ImageView.init(&ctx.graphics, .{
        .image = checker_image.handle,
        .format = tex_format,
        .subresource_range = .{
            .aspect_mask = .{ .color_bit = true },
            .base_mip_level = 0,
            .level_count = 1,
            .base_array_layer = 0,
            .layer_count = 1,
        },
    });
    defer checker_view.deinit(&ctx.destroy_queue);

    var sampler = try base.Sampler.init(&ctx.graphics, .{});
    defer sampler.deinit(&ctx.destroy_queue);

    var vis = try runtime.Visbuffer.init(&ctx, render_extent);
    defer vis.deinit(&ctx);

    const set_layout = try ctx.graphics.dev.createDescriptorSetLayout(&.{
        .flags = .{ .update_after_bind_pool_bit = true },
        .binding_count = 1,
        .p_bindings = &.{
            base.vk.DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptor_type = .combined_image_sampler,
                .descriptor_count = 1,
                .stage_flags = .{ .fragment_bit = true },
                .p_immutable_samplers = null,
            },
        },
    }, null);
    defer ctx.graphics.dev.destroyDescriptorSetLayout(set_layout, null);

    const skinned_vert = shaders.get(.vertex_skinned_test);
    const skinned_vert_mod = try base.ShaderModule.init(&ctx, skinned_vert);
    defer skinned_vert_mod.deinit(&ctx);

    const frag = shaders.get(.fragment_mesh_test);
    const frag_mod = try base.ShaderModule.init(&ctx, frag);
    defer frag_mod.deinit(&ctx);

    const SkinnedPC = struct {
        vp: zm.Mat,
        geometry_address: u64,
        joints_address: u64,
    };

    var skinned_pipeline = try base.GraphicsPipeline.init(&ctx, .{
        .vertex = skinned_vert_mod,
        .fragment = frag_mod,
        .set_layouts = &.{set_layout},
        .color_attachments = &.{.{ .format = render_format }},
        .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(SkinnedPC, base.layout.scalar)),
    });
    skinned_pipeline.depth_test_enable = .true;
    skinned_pipeline.depth_write_enable = .true;
    skinned_pipeline.depth_compare_op = .less;
    defer skinned_pipeline.deinit(&ctx.graphics);

    var world_instances_buf = try base.Buffer.init(&ctx.graphics, .graphics, "world_instances_buf", Culling.world_instance_size, .{ .storage_buffer_bit = true }, .cpu_to_gpu_dynamic);
    defer world_instances_buf.deinit(&ctx.destroy_queue);
    {
        const mapped = world_instances_buf.mapped.?;
        @as(*u32, @ptrCast(@alignCast(mapped))).* = 0;
        @memcpy(mapped[4..][0..64], std.mem.asBytes(&mesh_world));
        world_instances_buf.flush(ctx.graphics.vma_alloc, 0, Culling.world_instance_size);
    }

    const max_instances = 64;
    var renderables_buf = try base.Buffer.init(&ctx.graphics, .graphics, "renderables_buf", @sizeOf(u32) + max_instances * @sizeOf(Culling.RenderableEntry), .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer renderables_buf.deinit(&ctx.destroy_queue);

    var draw_cmds_buf = try base.Buffer.init(&ctx.graphics, .graphics, "draw_cmds_buf", max_instances * 5 * @sizeOf(u32), .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer draw_cmds_buf.deinit(&ctx.destroy_queue);

    // Per-frame arena for skinned vertex cache
    const arena_elements: u32 = 64 * 1024 * 32;
    const arena_buf_size = @sizeOf(u32) * 2 + arena_elements * @sizeOf(u32);
    var arena_buf = try base.Buffer.init(&ctx.graphics, .graphics, "skinning_arena", arena_buf_size, .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer arena_buf.deinit(&ctx.destroy_queue);

    // Skinned draw cmds (indirect with extra fields), includes u32 count header
    var skinned_draw_cmds_buf = try base.Buffer.init(&ctx.graphics, .graphics, "skinned_draw_cmds_buf", @sizeOf(u32) + max_instances * 32, .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer skinned_draw_cmds_buf.deinit(&ctx.destroy_queue);

    // Skinned world instances (mesh_ix + joint_offset + transform)
    const skinned_instance_count: u32 = 2;
    var skinned_world_instances_buf = try base.Buffer.init(&ctx.graphics, .graphics, "skinned_world_instances_buf", skinned_instance_count * Culling.skinned_world_instance_size, .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .cpu_to_gpu_dynamic);
    defer skinned_world_instances_buf.deinit(&ctx.destroy_queue);

    var culling = try Culling.init(&ctx);
    defer culling.deinit(&ctx);

    const aspect = @as(f32, @floatFromInt(ctx.render_extent.width)) / @as(f32, @floatFromInt(ctx.render_extent.height));
    var cam = base.Camera.initLookAt(
        zm.f32x4(0, 0, 2, 1),
        zm.f32x4(0,0,0,1),
        std.math.pi / 4.0,
        aspect,
        0.01,
        100.0,
    );

    var mouse_captured = false;
    var camera_speed: f32 = 2.0;

    defer ctx.graphics.dev.deviceWaitIdle() catch {};
    var timer = base.timing.FrameTimer.init(1.0 / 60.0);
    while (!ctx.window.shouldClose()) {
        timer.tick();
        base.zglfw.pollEvents();

        if (try ctx.prepare_frame() == .success) {
            try ctx.prepare_draw();

            imgui.newFrame(@floatCast(timer.frame_dt), ctx.swapchain.extent.width, ctx.swapchain.extent.height);

            while (timer.shouldUpdate()) {
                timer.consume();
                input.update(.{
                    .keyboard = !mouse_captured and imgui.wantCaptureKeyboard(),
                    .mouse = !mouse_captured and imgui.wantCaptureMouse(),
                });

                if (input.mouseJustPressed(.right)) {
                    mouse_captured = true;
                    imgui.enable(false);
                    try ctx.window.setInputMode(.cursor, .disabled);
                    if (base.zglfw.rawMouseMotionSupported()) {
                        try ctx.window.setInputMode(.raw_mouse_motion, true);
                    }
                }
                if (input.justPressed(.escape)) {
                    mouse_captured = false;
                    imgui.enable(true);
                    ctx.window.setInputMode(.cursor, .normal) catch {};
                    const pos = ctx.window.getCursorPos();
                    input.setMousePos(pos[0], pos[1]);
                }

                if (mouse_captured) {
                    const m = input.mouseDelta();
                    cam.rotate(@floatCast(m.x * 0.001), @floatCast(-m.y * 0.001));

                    const scroll = input.scrollDelta();
                    camera_speed *= @as(f32, @floatCast(1.0 + scroll.y * 0.1));
                    camera_speed = std.math.clamp(camera_speed, 0.1, 100.0);
                }

                const dt: f32 = @floatCast(timer.frame_dt);
                const speed = camera_speed * dt;
                if (input.isDown(.w)) cam.translate(cam.forward() * @as(zm.Vec, @splat(speed)));
                if (input.isDown(.s)) cam.translate(cam.forward() * @as(zm.Vec, @splat(-speed)));
                if (input.isDown(.a)) cam.translate(cam.right() * @as(zm.Vec, @splat(-speed)));
                if (input.isDown(.d)) cam.translate(cam.right() * @as(zm.Vec, @splat(speed)));
                if (input.isDown(.q)) cam.translate(cam.up() * @as(zm.Vec, @splat(-speed)));
                if (input.isDown(.e)) cam.translate(cam.up() * @as(zm.Vec, @splat(speed)));

                cam.update();
            }

            zgui.showDemoWindow(null);

            if (zgui.begin("Capture Status", .{ .popen = null })) {
                zgui.text("capture_keyboard: {any}", .{imgui.wantCaptureKeyboard()});
                zgui.text("capture_mouse: {any}", .{imgui.wantCaptureMouse()});

                zgui.text("x: {any}, y: {any}, z: {any}", .{cam.position[0], cam.position[1], cam.position[2]});
            }
            zgui.end();

            const frame = ctx.frames[ctx.current_frame];
            const rt = ctx.renderTarget();
            const dt = ctx.depthTarget();
            const dp = ctx.descriptorPool();

            const set_id = try dp.newSet(&ctx.graphics.dev, set_layout);
            dp.beginUpdate(set_id);
            try dp.updateSampledImage(gpa, 0, .shader_read_only_optimal, checker_view.handle, sampler.handle);
            dp.endUpdate(&ctx.graphics.dev);

            const anim_time = @mod(@as(f32, @floatCast(base.zglfw.getTime())), anims[1].duration);
            {
                const buf = &joint_matrices_bufs[ctx.current_frame];
                const mats: []zm.Mat = @as([*]zm.Mat, @alignCast(@ptrCast(buf.mapped.?)))[0..skin.skeleton_node_indices.len];
                try anims[1].evaluate(gpa, &skeleton, &skin, anim_time, mats);
                if (!buf.coherent) buf.flush(ctx.graphics.vma_alloc, 0, buf.size);
            }

            var rg = base.RenderGraph.init(gpa);
            defer rg.deinit();

            const rt_ref = try rg.addImage(.{
                .image = rt.image,
                .start_usage = .{
                    .stage = .{ .color_attachment_output_bit = true },
                    .access = .{ .color_attachment_write_bit = true, .color_attachment_read_bit = true },
                    .layout = .color_attachment_optimal,
                },
                .end_usage = .{
                    .stage = .{ .all_transfer_bit = true },
                    .access = .{ .transfer_read_bit = true },
                    .layout = .transfer_src_optimal,
                },
            });

            const depth_ref = try rg.addImage(.{
                .image = dt.image,
                .aspect = .{ .depth_bit = true },
                .start_usage = .{
                    .stage = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
                    .access = .{ .depth_stencil_attachment_read_bit = true, .depth_stencil_attachment_write_bit = true },
                    .layout = .depth_stencil_attachment_optimal,
                },
                .end_usage = .{
                    .stage = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
                    .access = .{ .depth_stencil_attachment_read_bit = true, .depth_stencil_attachment_write_bit = true },
                    .layout = .depth_stencil_attachment_optimal,
                },
            });

            const mesh_descs_ref = try rg.addBuffer(.{
                .buffer = mh.mesh_desc_buf,
                .offset = 0,
                .size = mh.mesh_desc_buf.size,
                .start_usage = .{
                    .stage = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
            });

            const lod_entries_ref = try rg.addBuffer(.{
                .buffer = mh.lod_entry_buf,
                .offset = 0,
                .size = mh.lod_entry_buf.size,
                .start_usage = .{
                    .stage = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
            });

            const world_instances_ref = try rg.addBuffer(.{
                .buffer = world_instances_buf,
                .offset = 0,
                .size = world_instances_buf.size,
                .start_usage = .{
                    .stage = .{ .compute_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .compute_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
            });

            const renderables_ref = try rg.addBuffer(.{
                .buffer = renderables_buf,
                .offset = 0,
                .size = renderables_buf.size,
                .start_usage = .{
                    .stage = .{ .all_commands_bit = true },
                    .access = .{ .memory_write_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .draw_indirect_bit = true, .vertex_shader_bit = true },
                    .access = .{ .indirect_command_read_bit = true, .shader_read_bit = true },
                },
            });

            const draw_cmds_ref = try rg.addBuffer(.{
                .buffer = draw_cmds_buf,
                .offset = 0,
                .size = draw_cmds_buf.size,
                .start_usage = .{
                    .stage = .{ .all_commands_bit = true },
                    .access = .{ .memory_write_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .draw_indirect_bit = true },
                    .access = .{ .indirect_command_read_bit = true },
                },
            });

            const skinned_world_instances_ref = try rg.addBuffer(.{
                .buffer = skinned_world_instances_buf,
                .offset = 0,
                .size = skinned_world_instances_buf.size,
                .start_usage = .{
                    .stage = .{ .compute_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .compute_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
            });

            const arena_ref = try rg.addBuffer(.{
                .buffer = arena_buf,
                .offset = 0,
                .size = arena_buf.size,
                .start_usage = .{
                    .stage = .{ .all_commands_bit = true },
                    .access = .{ .memory_write_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .vertex_shader_bit = true },
                    .access = .{ .shader_storage_write_bit = true, .shader_storage_read_bit = true },
                },
            });

            const skinned_draw_cmds_ref = try rg.addBuffer(.{
                .buffer = skinned_draw_cmds_buf,
                .offset = 0,
                .size = skinned_draw_cmds_buf.size,
                .start_usage = .{
                    .stage = .{ .all_commands_bit = true },
                    .access = .{ .memory_write_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .draw_indirect_bit = true, .vertex_shader_bit = true },
                    .access = .{ .indirect_command_read_bit = true, .shader_read_bit = true },
                },
            });

            const zero_pass = try rg.addTransferPass(.{});
            try zero_pass.fillBuffer(.{ .buffer = renderables_ref, .size = renderables_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = draw_cmds_ref, .size = draw_cmds_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = arena_ref, .size = @sizeOf(u32) }); // reset arena write_offset
            try zero_pass.fillBuffer(.{ .buffer = skinned_draw_cmds_ref, .size = @sizeOf(u32) }); // reset skinned count

            var cull_pc_buf: [Culling.CullPC.size]u8 = undefined;
            try culling.cull(&rg, .{
                .vp = cam.view_projection,
                .world_instances_ref = world_instances_ref,
                .mesh_descs_ref = mesh_descs_ref,
                .lod_entries_ref = lod_entries_ref,
                .renderables_ref = renderables_ref,
                .draw_cmds_ref = draw_cmds_ref,
                .instance_count = 0,
                .max_draw_count = max_instances,
                .screen_width = @floatFromInt(render_extent.width),
                .screen_height = @floatFromInt(render_extent.height),
                .fov_y = std.math.pi / 4.0,
                .camera_pos = .{ cam.position[0], cam.position[1], cam.position[2] },
                .pc_buf = &cull_pc_buf,
            });

            const joints_ref = try rg.addBuffer(.{
                .buffer = joint_matrices_bufs[ctx.current_frame],
                .offset = 0,
                .size = joint_matrices_bufs[ctx.current_frame].size,
                .start_usage = .{
                    .stage = .{ .compute_shader_bit = true, .vertex_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
                .end_usage = .{
                    .stage = .{ .compute_shader_bit = true, .vertex_shader_bit = true },
                    .access = .{ .shader_storage_read_bit = true },
                },
            });

            // Upload skinned world instances
            {
                const mapped = skinned_world_instances_buf.mapped.?;
                const instances = std.mem.bytesAsSlice(u32, mapped[0..skinned_instance_count * Culling.skinned_world_instance_size]);
                // body_mesh at mesh_desc_ix 0, helmet at 1, both use joint_offset 0
                instances[0] = 0; // mesh_desc_ix
                instances[1] = 0; // joint_offset
                // transform at offset 2 (two u32s skipped)
                @memcpy(std.mem.sliceAsBytes(instances[2..18]), std.mem.asBytes(&mesh_world));
                const base_off = Culling.skinned_world_instance_size;
                const inst2 = std.mem.bytesAsSlice(u32, mapped[base_off..][0..Culling.skinned_world_instance_size]);
                inst2[0] = 1; // mesh_desc_ix
                inst2[1] = 0; // joint_offset
                @memcpy(std.mem.sliceAsBytes(inst2[2..18]), std.mem.asBytes(&mesh_world));
                if (!skinned_world_instances_buf.coherent)
                    skinned_world_instances_buf.flush(ctx.graphics.vma_alloc, 0, skinned_world_instances_buf.size);
            }

            var anim_cull_pc_buf: [Culling.AnimatedCullPC.size]u8 = undefined;
            try culling.cullAnimated(&rg, .{
                .vp = cam.view_projection,
                .world_instances_ref = skinned_world_instances_ref,
                .mesh_descs_ref = mesh_descs_ref,
                .lod_entries_ref = lod_entries_ref,
                .renderables_ref = renderables_ref,
                .draw_cmds_ref = skinned_draw_cmds_ref,
                .arena_ref = arena_ref,
                .joints_ref = joints_ref,
                .arena_capacity = arena_elements,
                .instance_count = skinned_instance_count,
                .max_draw_count = max_instances,
                .screen_width = @floatFromInt(render_extent.width),
                .screen_height = @floatFromInt(render_extent.height),
                .fov_y = std.math.pi / 4.0,
                .camera_pos = .{ cam.position[0], cam.position[1], cam.position[2] },
                .pc_buf = &anim_cull_pc_buf,
            });

            const vis_ref, _ = try vis.visbuffer_ref(ctx.current_frame, &rg);
            var vb_pc_buf: [runtime.Visbuffer.pc_size]u8 = undefined;
            try vis.raster(&rg, .{
                .current_frame = ctx.current_frame,
                .vis_ref = vis_ref,
                .vp = cam.view_projection,
                .depth_attachment = .{
                    .image = depth_ref,
                    .view = dt.view,
                },
                .renderables_buf_ref = renderables_ref,
                .renderables_buf_address = renderables_buf.address,
                .draw_cmds_buf_ref = draw_cmds_ref,
                .draw_cmds_buf_address = draw_cmds_buf.address,
                .max_draw_count = max_instances,
                .pc_buf = &vb_pc_buf,
            });

            var skinned_vb_pc_buf: [runtime.Visbuffer.skinned_pc_size]u8 = undefined;
            try vis.rasterSkinned(&rg, .{
                .current_frame = ctx.current_frame,
                .vis_ref = vis_ref,
                .vp = cam.view_projection,
                .depth_attachment = .{
                    .image = depth_ref,
                    .view = dt.view,
                },
                .renderables_buf_ref = renderables_ref,
                .renderables_buf_address = renderables_buf.address,
                .draw_cmds_buf_ref = skinned_draw_cmds_ref,
                .draw_cmds_buf_address = skinned_draw_cmds_buf.address,
                .joints_buf_address = joint_matrices_bufs[ctx.current_frame].address,
                .max_draw_count = max_instances,
                .pc_buf = &skinned_vb_pc_buf,
            });

            const gpass = try rg.addGraphicsPass(.{
                .pipeline = &skinned_pipeline,
                .color_attachments = &.{.{ .image = rt_ref, .view = rt.view, .load_op = .clear, .store_op = .store, .clear_color = .{ .float_32 = .{ 0.1, 0.2, 0.6, 1.0 } } }},
                .depth_attachment = .{ .image = depth_ref, .view = dt.view },
                .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = render_extent },
                .descriptor_sets = &.{set_id},
            });

            var pc_buf1: [base.push_constant.size(SkinnedPC, base.layout.scalar)]u8 = undefined;
            base.push_constant.write(SkinnedPC, &pc_buf1, .{ .vp = zm.mul(mesh_world, cam.view_projection), .geometry_address = body_mesh.lods[0].geometry_buffer.?.@"0".address, .joints_address = joint_matrices_bufs[ctx.current_frame].address }, base.layout.scalar);
            try gpass.draw(.{ .push_constant = pc_buf1[0..], .vertex_count = body_mesh.lods[0].draw_count });

            var pc_buf2: [base.push_constant.size(SkinnedPC, base.layout.scalar)]u8 = undefined;
            base.push_constant.write(SkinnedPC, &pc_buf2, .{ .vp = zm.mul(mesh_world, cam.view_projection), .geometry_address = helmet_mesh.lods[0].geometry_buffer.?.@"0".address, .joints_address = joint_matrices_bufs[ctx.current_frame].address }, base.layout.scalar);
            try gpass.draw(.{ .push_constant = pc_buf2[0..], .vertex_count = helmet_mesh.lods[0].draw_count });

            try rg.run(&ctx.graphics, frame.cmd_buf, ctx.descriptorPool());

            ctx.end_drawing();

            imgui.render(&ctx);
        }

        if (try ctx.end_frame(gpa) == .recreated) {}
    }
}
