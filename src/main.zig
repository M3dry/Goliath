const std = @import("std");
const runtime = @import("runtime");

const base = runtime.base;
const zgui = base.zgui;
const zm = base.zmath;

const zmesh = runtime.zmesh;

const shaders = @import("shaders");

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

    var shape = zmesh.Shape.initTorus(32, 32, 0.3);
    defer shape.deinit();
    shape.computeNormals();

    const mesh_io = try runtime.MeshIO.fromShape(gpa, shape);
    defer mesh_io.deinit(gpa);

    var test_mesh = try runtime.Mesh.init(gpa, mesh_io.source);
    defer test_mesh.deinit(gpa);

    try mh.registerMesh(&test_mesh, gpa, &ctx.graphics, &transport, &ctx.destroy_queue);
    defer mh.unregisterMesh(&test_mesh, &ctx.destroy_queue, &transport);

    try mh.flushDescriptorArrays(&ctx.graphics, &ctx.destroy_queue, &transport);

    {
        const geo_buf = test_mesh.lods[0].geometry_buffer.?;
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

    const vert = shaders.get(.vertex_mesh_test);
    const vert_mod = try base.ShaderModule.init(&ctx, vert);
    defer vert_mod.deinit(&ctx);

    const frag = shaders.get(.fragment_mesh_test);
    const frag_mod = try base.ShaderModule.init(&ctx, frag);
    defer frag_mod.deinit(&ctx);

    const PC = struct {
        vp: zm.Mat,
        geometry_address: u64,
    };

    var pipeline = try base.GraphicsPipeline.init(&ctx, .{
        .vertex = vert_mod,
        .fragment = frag_mod,
        .set_layouts = &.{set_layout},
        .color_attachments = &.{.{ .format = render_format }},
        .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(PC)),
    });
    pipeline.depth_test_enable = .true;
    pipeline.depth_write_enable = .true;
    pipeline.depth_compare_op = .less;
    defer pipeline.deinit(&ctx);

    const aspect = @as(f32, @floatFromInt(ctx.render_extent.width)) / @as(f32, @floatFromInt(ctx.render_extent.height));
    var cam = base.Camera.init(
        zm.f32x4(0, 0, -2, 1),
        0,
        0,
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
            }
            zgui.end();

            const frame = ctx.frames[ctx.current_frame];
            const rt = ctx.renderTarget();
            const dt = ctx.depthTarget();

            const geo_buf = test_mesh.lods[0].geometry_buffer.?;
            var pc_buf: [base.push_constant.size(PC)]u8 = undefined;
            base.push_constant.write(PC, &pc_buf, .{ .vp = cam.view_projection, .geometry_address = geo_buf.@"0".address });

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

            const dp = ctx.descriptorPool();
            const set_id = try dp.newSet(&ctx.graphics.dev, set_layout);
            dp.beginUpdate(set_id);
            try dp.updateSampledImage(gpa, 0, .shader_read_only_optimal, checker_view.handle, sampler.handle);
            dp.endUpdate(&ctx.graphics.dev);

            const gpass = try rg.addGraphicsPass(.{
                .pipeline = &pipeline,
                .color_attachments = &.{.{ .image = rt_ref, .view = rt.view, .load_op = .clear, .store_op = .store, .clear_color = .{ .float_32 = .{ 0.1, 0.2, 0.6, 1.0 } } }},
                .depth_attachment = .{ .image = depth_ref, .view = dt.view },
                .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = render_extent },
                .descriptor_sets = &.{set_id},
            });
            try gpass.draw(.{ .push_constant = pc_buf[0..], .vertex_count = test_mesh.lods[0].vertex_count });

            try rg.run(&ctx.graphics, frame.cmd_buf, ctx.descriptorPool());

            ctx.end_drawing();

            imgui.render(&ctx);
        }

        if (try ctx.end_frame(gpa) == .recreated) {}
    }
}
