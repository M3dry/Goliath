const std = @import("std");
const runtime = @import("runtime");
const base = runtime.base;
const zm = base.zmath;
const zmesh = runtime.zmesh;
const shaders = @import("shaders");

const Config = struct {
    discard_frames: u32 = 60,
    bench_frames: u32 = 300,
    sphere_count: u32 = 100,
    subdiv: i32 = 5,
    draws_per_frame: u32 = 1,
};

fn parseArgs(args: []const [*:0]const u8) Config {
    var config = Config{};
    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const arg = std.mem.span(args[i]);
        if (std.mem.eql(u8, arg, "--discard")) {
            i += 1;
            if (i < args.len) config.discard_frames = std.fmt.parseInt(u32, std.mem.span(args[i]), 10) catch config.discard_frames;
        } else if (std.mem.eql(u8, arg, "--frames")) {
            i += 1;
            if (i < args.len) config.bench_frames = std.fmt.parseInt(u32, std.mem.span(args[i]), 10) catch config.bench_frames;
        } else if (std.mem.eql(u8, arg, "--spheres")) {
            i += 1;
            if (i < args.len) config.sphere_count = std.fmt.parseInt(u32, std.mem.span(args[i]), 10) catch config.sphere_count;
        } else if (std.mem.eql(u8, arg, "--subdiv")) {
            i += 1;
            if (i < args.len) config.subdiv = std.fmt.parseInt(i32, std.mem.span(args[i]), 10) catch config.subdiv;
        } else if (std.mem.eql(u8, arg, "--draws")) {
            i += 1;
            if (i < args.len) config.draws_per_frame = std.fmt.parseInt(u32, std.mem.span(args[i]), 10) catch config.draws_per_frame;
        }
    }
    return config;
}

const PC = struct {
    vp: zm.Mat,
    geometry_address: u64,
};

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const config = parseArgs(init.minimal.args.vector);
    const run_until_close = config.bench_frames == 0;

    var ctx = try base.Ctx.init(gpa, "Vertex Benchmark", .{
        .name = "Vertex Benchmark",
        .size = .{ .dims = .{ 64, 64 } },
        .resizable = false,
        .render_extent = .{ .width = 64, .height = 64 },
        .render_format = .r32g32b32a32_sfloat,
    });
    defer ctx.deinit(gpa);

    var transport: base.Transport = undefined;
    try transport.init(&ctx.graphics, gpa, init.io);
    defer transport.deinit(&ctx.graphics);

    var mh = runtime.MeshHandler.empty;
    defer mh.deinit(gpa, &ctx.destroy_queue, &transport);

    zmesh.init(gpa);
    defer zmesh.deinit();

    var mesh_worlds = std.ArrayList(zm.Mat).empty;
    defer mesh_worlds.deinit(gpa);

    const grid_side_f = std.math.sqrt(@as(f32, @floatFromInt(config.sphere_count)));
    const grid_side = @as(u32, @intFromFloat(@ceil(grid_side_f)));
    const spacing: f32 = 3.0;
    const half_extent = spacing * grid_side_f / 2.0;

    for (0..config.sphere_count) |i| {
        const ix = @as(f32, @floatFromInt(i % grid_side));
        const iz = @as(f32, @floatFromInt(i / grid_side));
        try mesh_worlds.append(gpa, zm.translation(ix * spacing - half_extent, 0.0, iz * spacing - half_extent));
    }

    var shape = zmesh.Shape.initSubdividedSphere(config.subdiv);
    defer shape.deinit();
    shape.computeNormals();

    const mesh_io = try runtime.MeshIO.fromShape(gpa, shape);
    defer mesh_io.deinit(gpa);

    var mesh = try runtime.Mesh.init(gpa, mesh_io.source);
    defer mesh.deinit(gpa);

    try mh.registerMesh(&mesh, gpa, &ctx.graphics, &transport, &ctx.destroy_queue);
    defer mh.unregisterMesh(&mesh, &ctx.destroy_queue, &transport);
    try mh.flushDescriptorArrays(&ctx.graphics, &ctx.destroy_queue, &transport);

    if (mesh.lods[0].geometry_buffer) |geo_buf| {
        while (!try transport.isReady(geo_buf.@"1")) {
            try transport.drain(&ctx.graphics);
            std.Thread.yield() catch {};
        }
    }
    while (!try transport.isReady(mh.ticket)) {
        try transport.drain(&ctx.graphics);
        std.Thread.yield() catch {};
    }

    const query_count = base.Ctx.frames_in_flight * 2;
    const query_pool = try ctx.graphics.dev.createQueryPool(&.{
        .query_type = .timestamp,
        .query_count = query_count,
        .pipeline_statistics = .{},
    }, null);
    defer ctx.graphics.dev.destroyQueryPool(query_pool, null);

    const vert = shaders.get(.vertex_dynamic_pulling);
    const vert_mod = try base.ShaderModule.init(&ctx, vert);
    defer vert_mod.deinit(&ctx);

    const frag = shaders.get(.fragment_solid_color);
    const frag_mod = try base.ShaderModule.init(&ctx, frag);
    defer frag_mod.deinit(&ctx);

    var pipeline = try base.GraphicsPipeline.init(&ctx, .{
        .vertex = vert_mod,
        .fragment = frag_mod,
        .set_layouts = &.{},
        .color_attachments = &.{.{ .format = ctx.render_format }},
        // .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(PC, base.layout.scalar)),
    });
    // pipeline.depth_test_enable = .true;
    // pipeline.depth_write_enable = .true;
    // pipeline.depth_compare_op = .less;
    defer pipeline.deinit(&ctx);

    const aspect = @as(f32, @floatFromInt(ctx.render_extent.width)) / @as(f32, @floatFromInt(ctx.render_extent.height));
    var cam = base.Camera.initLookAt(
        zm.f32x4(0.0, 2.0, 5.0, 1.0),
        zm.f32x4(0.0, 0.0, 0.0, 1.0),
        std.math.pi / 4.0,
        aspect,
        0.01,
        100.0,
    );
    cam.update();

    var frame_counter: u32 = 0;
    var warmup_counter: u32 = 0;
    var deltas = std.ArrayList(u64).empty;
    defer deltas.deinit(gpa);

    defer ctx.graphics.dev.deviceWaitIdle() catch {};

    while (true) {
        base.zglfw.pollEvents();
        if (ctx.window.shouldClose()) break;

        if (try ctx.prepare_frame() == .success) {
            const cmd = ctx.frames[ctx.current_frame].cmd_buf;

            if (frame_counter >= base.Ctx.frames_in_flight) {
                const read_slot = ctx.current_frame * 2;
                var ts: [2]u64 = undefined;
                _ = ctx.graphics.dev.getQueryPoolResults(
                    query_pool,
                    read_slot,
                    2,
                    @sizeOf([2]u64),
                    @ptrCast(&ts),
                    @sizeOf(u64),
                    .{ .@"64_bit" = true, .wait_bit = true },
                ) catch {};

                if (warmup_counter >= config.discard_frames) {
                    try deltas.append(gpa, ts[1] - ts[0]);
                }
                warmup_counter += 1;
            }

            try ctx.prepare_draw();

            const slot = ctx.current_frame * 2;
            ctx.graphics.dev.cmdResetQueryPool(cmd, query_pool, slot, 2);
            ctx.graphics.dev.cmdWriteTimestamp2(cmd, .{ .all_commands_bit = true }, query_pool, slot);

            const rt = ctx.renderTarget();
            // const dt = ctx.depthTarget();

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
            // const depth_ref = try rg.addImage(.{
            //     .image = dt.image,
            //     .aspect = .{ .depth_bit = true },
            //     .start_usage = .{
            //         .stage = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
            //         .access = .{ .depth_stencil_attachment_read_bit = true, .depth_stencil_attachment_write_bit = true },
            //         .layout = .depth_stencil_attachment_optimal,
            //     },
            //     .end_usage = .{
            //         .stage = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
            //         .access = .{ .depth_stencil_attachment_read_bit = true, .depth_stencil_attachment_write_bit = true },
            //         .layout = .depth_stencil_attachment_optimal,
            //     },
            // });

            const gpass = try rg.addGraphicsPass(.{
                .pipeline = &pipeline,
                .color_attachments = &.{.{ .image = rt_ref, .view = rt.view, .load_op = .clear, .store_op = .store, .clear_color = .{ .float_32 = .{ 0.1, 0.2, 0.6, 1.0 } } }},
                // .depth_attachment = .{ .image = depth_ref, .view = dt.view },
                .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = ctx.render_extent },
                .descriptor_sets = &.{},
            });

            const geo_buf = mesh.lods[0].geometry_buffer.?;
            for (mesh_worlds.items) |world| {
                for (0..config.draws_per_frame) |_| {
                    var pc_buf: [base.push_constant.size(PC, base.layout.scalar)]u8 = undefined;
                    base.push_constant.write(PC, &pc_buf, .{
                        .vp = zm.mul(world, cam.view_projection),
                        .geometry_address = geo_buf.@"0".address,
                    }, base.layout.scalar);
                    try gpass.draw(.{ .push_constant = pc_buf[0..], .vertex_count = mesh.lods[0].draw_count });
                }
            }

            try rg.run(&ctx.graphics, cmd, ctx.descriptorPool());

            ctx.graphics.dev.cmdWriteTimestamp2(cmd, .{ .all_commands_bit = true }, query_pool, slot + 1);

            ctx.end_drawing();

            const acq = ctx.frames[ctx.current_frame].acquired_swapchain orelse unreachable;
            ctx.graphics.dev.cmdPipelineBarrier2(cmd, &.{
                .image_memory_barrier_count = 1,
                .p_image_memory_barriers = &.{base.vk.ImageMemoryBarrier2{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = .{ .all_commands_bit = true },
                    .dst_access_mask = .{},
                    .old_layout = .transfer_dst_optimal,
                    .new_layout = .present_src_khr,
                    .src_queue_family_index = ctx.graphics.graphics_family,
                    .dst_queue_family_index = ctx.graphics.graphics_family,
                    .subresource_range = base.util.fullRange(.{ .color_bit = true }),
                    .image = ctx.swapchain.images[acq].image,
                }},
            });
        }

        if (try ctx.end_frame(gpa) == .recreated) {}
        frame_counter += 1;

        if (!run_until_close and deltas.items.len >= config.bench_frames) break;
    }

    if (deltas.items.len > 0) {
        const period = ctx.graphics.props.limits.timestamp_period;
        std.mem.sort(u64, deltas.items, {}, std.sort.asc(u64));
        const n = deltas.items.len;
        const median = deltas.items[n / 2];
        const p95 = deltas.items[@min(@as(usize, @intFromFloat(@as(f64, @floatFromInt(n)) * 0.95)), n - 1)];
        const p99 = deltas.items[@min(@as(usize, @intFromFloat(@as(f64, @floatFromInt(n)) * 0.99)), n - 1)];
        const median_ns = @as(f64, @floatFromInt(median)) * period;
        const p95_ns = @as(f64, @floatFromInt(p95)) * period;
        const p99_ns = @as(f64, @floatFromInt(p99)) * period;

        std.debug.print("\n=== Vertex Pulling Benchmark ===\n", .{});
        std.debug.print("  Draw count: {}\n", .{ mesh.lods[0].draw_count });
        std.debug.print("  Meshes: {}  |  Subdiv: {}  |  Draws/frame: {}\n", .{ config.sphere_count, config.subdiv, config.draws_per_frame });
        std.debug.print("  Discarded: {}  |  Samples: {}\n\n", .{ config.discard_frames, deltas.items.len });
        std.debug.print("  Median: {d:.0} ns  ({d:.4} ms)\n", .{ median_ns, median_ns / 1_000_000.0 });
        std.debug.print("  P95:    {d:.0} ns  ({d:.4} ms)\n", .{ p95_ns, p95_ns / 1_000_000.0 });
        std.debug.print("  P99:    {d:.0} ns  ({d:.4} ms)\n", .{ p99_ns, p99_ns / 1_000_000.0 });
    }
}
