const std = @import("std");
const base = @import("base");
const zgui = base.zgui;

const shaders = @import("shaders");

pub fn main(init: std.process.Init) !void {

    const gpa = init.gpa;
    var ctx = try base.Ctx.init(gpa, "Demo", .{
        .resizable = false,
        .size = .fullscreen,
        .render_extent = .{ .width = 1920, .height = 1080 },
        .blit_strategy = .letterbox,
    });
    defer ctx.deinit(gpa);

    var input = base.input.InputState{};
    input.init(ctx.window);
    defer input.deinit(ctx.window);

    var imgui = try base.imgui.ImguiState.init(gpa, &ctx);
    defer imgui.deinit(ctx.graphics.dev);

    var gbuf = try base.Buffer.init(&ctx, .graphics, "Test buffer", @sizeOf(i64)*100, .{ .transfer_dst_bit = true, .storage_buffer_bit = true }, false);
    defer gbuf.deinit(&ctx);

    var tex_pool = try base.TexturePool.init(ctx.graphics.dev, 1000);
    defer tex_pool.deinit(ctx.graphics.dev);

    const shader = shaders.get(.compute_culling);
    const shader_mod = try base.ShaderModule.init(&ctx, shader, .{ .compute_bit = true });
    _ = shader_mod;

    var timer = base.timing.FrameTimer.init(1.0 / 60.0);
    while (!ctx.window.shouldClose()) {
        timer.tick();
        base.zglfw.pollEvents();

        if (try ctx.prepare_frame() == .success) {
            try ctx.prepare_draw();

            const win_size = ctx.window.getSize();
            imgui.newFrame(@floatCast(timer.frame_dt), @intCast(win_size[0]), @intCast(win_size[1]));

            while (timer.shouldUpdate()) {
                timer.consume();
                input.update(.{
                    .keyboard = imgui.wantCaptureKeyboard(),
                    .mouse = imgui.wantCaptureMouse(),
                });

                if (input.justPressed(.space)) {
                    std.log.info("space pressed", .{});
                }
                if (input.justReleased(.q)) {
                    std.log.info("q released", .{});
                }
                if (input.isRepeated(.a)) {
                    std.log.info("a repeating", .{});
                }
                if (input.justPressed(.w)) {
                    std.log.info("w pressed, mods: {any}", .{input.justPressedWith(.w).?});
                }
                if (input.anyJustPressed()) |key| {
                    std.log.info("any key pressed: {s}", .{@tagName(key)});
                }
                if (input.anyKeyDown()) {
                    std.log.info("some key is held", .{});
                }
                const pos = input.mousePos();
                const delta = input.mouseDelta();
                if (delta.x != 0 or delta.y != 0) {
                    std.log.info("mouse pos=({d:.1},{d:.1}) delta=({d:.1},{d:.1})", .{ pos.x, pos.y, delta.x, delta.y });
                }
                const scroll = input.scrollDelta();
                if (scroll.x != 0 or scroll.y != 0) {
                    std.log.info("scroll ({d:.1},{d:.1})", .{ scroll.x, scroll.y });
                }
                if (input.mouseJustPressed(.left)) {
                    std.log.info("left mouse pressed", .{});
                }
                if (input.mouseJustPressed(.right)) {
                    std.log.info("right mouse pressed", .{});
                }
                if (input.mouseJustReleased(.middle)) {
                    std.log.info("middle mouse released", .{});
                }
                for (input.charEvents()) |cp| {
                    var buf: [4]u8 = undefined;
                    const len = std.unicode.utf8Encode(cp, &buf) catch continue;
                    std.log.info("char input: {s}", .{buf[0..len]});
                }
                input.clearCharEvents();
            }

            zgui.showDemoWindow(null);

            if (zgui.begin("Capture Status", .{ .popen = null })) {
                zgui.text("capture_keyboard: {any}", .{imgui.wantCaptureKeyboard()});
                zgui.text("capture_mouse: {any}", .{imgui.wantCaptureMouse()});
            }
            zgui.end();

            const frame = ctx.frames[ctx.curent_frame];
            const qf = ctx.graphics.graphics_family;

            const rt = ctx.renderTarget();
            {
                const barrier = base.vk.ImageMemoryBarrier2{
                    .src_stage_mask = .{ .color_attachment_output_bit = true },
                    .src_access_mask = .{ .color_attachment_write_bit = true, .color_attachment_read_bit = true },
                    .dst_stage_mask = .{ .all_transfer_bit = true },
                    .dst_access_mask = .{ .transfer_read_bit = true },
                    .old_layout = .color_attachment_optimal,
                    .new_layout = .transfer_dst_optimal,
                    .src_queue_family_index = qf,
                    .dst_queue_family_index = qf,
                    .subresource_range = .{
                        .aspect_mask = .{ .color_bit = true },
                        .base_mip_level = 0,
                        .level_count = base.vk.REMAINING_MIP_LEVELS,
                        .base_array_layer = 0,
                        .layer_count = base.vk.REMAINING_ARRAY_LAYERS,
                    },
                    .image = rt.image,
                };
                ctx.graphics.dev.cmdPipelineBarrier2(frame.cmd_buf, &.{
                    .image_memory_barrier_count = 1,
                    .p_image_memory_barriers = (&barrier)[0..1],
                });
            }

            ctx.graphics.dev.cmdClearColorImage(
                frame.cmd_buf,
                rt.image,
                .transfer_dst_optimal,
                &base.vk.ClearColorValue{ .float_32 = .{ 0.1, 0.2, 0.6, 1.0 } },
                (&base.vk.ImageSubresourceRange{
                    .aspect_mask = .{ .color_bit = true },
                    .base_mip_level = 0,
                    .level_count = base.vk.REMAINING_MIP_LEVELS,
                    .base_array_layer = 0,
                    .layer_count = base.vk.REMAINING_ARRAY_LAYERS,
                })[0..1],
            );

            {
                const barrier = base.vk.ImageMemoryBarrier2{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = .{ .all_transfer_bit = true },
                    .dst_access_mask = .{ .transfer_read_bit = true },
                    .old_layout = .transfer_dst_optimal,
                    .new_layout = .transfer_src_optimal,
                    .src_queue_family_index = qf,
                    .dst_queue_family_index = qf,
                    .subresource_range = .{
                        .aspect_mask = .{ .color_bit = true },
                        .base_mip_level = 0,
                        .level_count = base.vk.REMAINING_MIP_LEVELS,
                        .base_array_layer = 0,
                        .layer_count = base.vk.REMAINING_ARRAY_LAYERS,
                    },
                    .image = rt.image,
                };
                ctx.graphics.dev.cmdPipelineBarrier2(frame.cmd_buf, &.{
                    .image_memory_barrier_count = 1,
                    .p_image_memory_barriers = (&barrier)[0..1],
                });
            }

            ctx.end_drawing();

            imgui.render(&ctx);
        }

        if (try ctx.end_frame(gpa) == .recreated) {
            // rebuild user frame structures
        }
    }
}
