const std = @import("std");
const runtime = @import("runtime");
const shaders = @import("shaders");
const build_options = @import("build_options");

const rebuild_assets = build_options.asset_system_rebuild;

const base = runtime.base;
const zgui = base.zgui;
const zm = base.zmath;

const zmesh = runtime.zmesh;

const Culling = runtime.Culling;

fn waitForTick(ctx: base.Ctx.Query(&.{ .transport, .device, .graphics_queue }), tick: base.Transport.Ticket) !void {
    while (!try ctx.view.transport.isReady(tick)) {
        try ctx.view.transport.drain(.from(ctx));
        std.Thread.yield() catch {};
    }
}

const Slot = enum { albedo, metallic_roughness, normal, occlusion, emissive };

/// name of the sampled texture backing a material slot; missing slots fall back
/// to the white / flat-normal entries
fn slotName(slot: Slot, texture_index: u32, buf: *[64]u8) []const u8 {
    if (texture_index == runtime.PbrShading.no_texture) {
        return switch (slot) {
            .normal => "sampled_flat_normal",
            else => "sampled_white",
        };
    }
    return std.fmt.bufPrint(buf, "sampled_gltf_texture_{d}", .{texture_index}) catch unreachable;
}

/// ingest an asset into the asset system and finalize it as a named entry;
/// the underlying file is kept on disk for the manifest
fn ingestAndFinalize(asset_system: *runtime.AssetSystem, data: runtime.AssetSystem.KindData, name: []const u8) !runtime.AssetSystem.Gid {
    var future = try asset_system.ingestAsset(data, name);
    var entry = try future.await(asset_system.io());
    entry.name = name;
    return asset_system.finalizeAsset(entry, true);
}

/// build a geometry ingest payload from a parsed mesh lod
fn geometryIngest(lod: runtime.Mesh.Lod) runtime.AssetSystem.GeometryRegistry.IngestGeometry {
    const g = lod.geometry.geo;
    return .{
        .indexed_tangents = (g.stride & runtime.Mesh.Geometry.indexed_tangents_bit) != 0,
        .stride = @intCast(g.stride & runtime.Mesh.Geometry.stride_mask),
        .position_offset = g.position_offset,
        .normal_offset = g.normal_offset,
        .tangent_offset = g.tangent_offset,
        .color0_offset = g.color0_offset,
        .texcoord0_offset = g.texcoord0_offset,
        .texcoord1_offset = g.texcoord1_offset,
        .texcoord2_offset = g.texcoord2_offset,
        .texcoord3_offset = g.texcoord3_offset,
        .joints0_offset = g.joints0_offset,
        .weights0_offset = g.weights0_offset,
        .data = lod.geometry.data,
    };
}

/// single-lod mesh blob pointing at an ingested geometry; material gids are
/// placeholder 0,0 until material handling exists. `lods_storage` backs the
/// returned slice; the caller awaits the ingestion before it goes out of scope.
fn meshBlob(mesh: *const runtime.Mesh, geometry_gid: runtime.AssetSystem.Gid, lods_storage: *[1]runtime.AssetSystem.MeshRegistry.LodEntryBlob) runtime.AssetSystem.MeshRegistry.MeshDescBlob {
    const lod = mesh.lods[0];
    lods_storage.* = .{.{
        .geometry = geometry_gid,
        .vertex_count = lod.vertex_count,
        .draw_count = lod.draw_count,
        .material_schema = .{ .gen = 0, .slot = 0 },
        .material_instance = .{ .gen = 0, .slot = 0 },
        .error_metric = lod.error_metric,
    }};
    return .{
        .lods = lods_storage,
        .aabb_min = .{ mesh.aabb.min[0], mesh.aabb.min[1], mesh.aabb.min[2] },
        .aabb_max = .{ mesh.aabb.max[0], mesh.aabb.max[1], mesh.aabb.max[2] },
    };
}

pub fn main(init: std.process.Init) !void {
    var file_subscriber: base.zprobe.FileSubscriber = undefined;
    base.zprobe.FileSubscriber.init(&file_subscriber, init.io, std.Io.File.stdout(), &.{}, "Stdout");
    defer file_subscriber.writer.flush() catch {};
    file_subscriber.subscriber.register();

    const gpa = init.gpa;
    const render_extent: base.vk.Extent2D = .{ .width = 1920, .height = 1080 };
    const render_format: base.vk.Format = .r32g32b32a32_sfloat;
    var ctx: base.Ctx = undefined;
    try ctx.init(gpa, init.io, "Demo", .{
        .name = "Demo",
        .resizable = false,
        .size = .fullscreen,
        .render_extent = render_extent,
        .blit_strategy = .letterbox,
        .render_format = render_format,
    });
    defer ctx.deinit(gpa);

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

    const mesh_world = zm.scaling(0.05, 0.05, 0.05);
    const mesh_world2: zm.Mat = zm.mul(zm.translation(50, 0, 0), zm.scaling(0.05, 0.05, 0.05));

    var joint_matrices_bufs: [base.Ctx.frames_in_flight]base.Buffer = undefined;
    for (&joint_matrices_bufs, 0..) |*buf, i| {
        buf.* = try base.Buffer.init(.from(&ctx), .graphics, "Joint matrices buffer", skin.skeleton_node_indices.len * @sizeOf(zm.Mat), .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .cpu_to_gpu_dynamic);
        _ = i;
    }
    defer for (&joint_matrices_bufs) |*buf| buf.deinit(.from(&ctx));

    var input = base.Input{};
    input.init(.from(&ctx));
    defer input.deinit(.from(&ctx));

    var imgui = try base.Imgui.init(gpa, .from(&ctx));
    defer imgui.deinit(.from(&ctx));

    // PBR material instance: gltf texture indices → texture pool indices, resolved
    // against the asset system's sampled textures.
    var inst = try runtime.PbrShading.PBRInstance.fromGltf(gltf_data, 0);

    const asset_dir = "testing_asset_system";
    if (rebuild_assets) {
        std.Io.Dir.deleteTree(.cwd(), init.io, asset_dir) catch {};
        try std.Io.Dir.createDir(.cwd(), init.io, asset_dir, .default_dir);
    }

    var manifest_file: ?std.Io.File = null;
    if (!rebuild_assets) {
        manifest_file = try std.Io.Dir.openFile(.cwd(), init.io, asset_dir ++ "/manifest.json", .{});
    }
    defer if (manifest_file) |*f| f.close(init.io);

    var asset_system: runtime.AssetSystem = undefined;
    if (rebuild_assets) {
        asset_system = try runtime.AssetSystem.init(.from(&ctx), gpa, gpa, .{ .default = .{ .path_prefix = asset_dir } });
    } else {
        var file_reader_buffer: [512]u8 = undefined;
        var file_reader = manifest_file.?.readerStreaming(init.io, &file_reader_buffer);
        asset_system = try runtime.AssetSystem.init(.from(&ctx), gpa, gpa, .{ .reader = &file_reader.interface });
    }
    defer asset_system.deinit(.from(&ctx));

    if (rebuild_assets) {
        // fallback 1x1 textures: white + flat normal
        const white = [4]u8{ 255, 255, 255, 255 };
        const flat_normal = [4]u8{ 128, 128, 255, 255 };
        const white_gid = try ingestAndFinalize(&asset_system, .{
            .texture = .{
                .default_image = .white,
                .data = .{ .decoded_blob = .{ .format = .r8g8b8a8_srgb, .width = 1, .height = 1, .blob = &white } },
            },
        }, "white");
        const flat_normal_gid = try ingestAndFinalize(&asset_system, .{
            .texture = .{
                .default_image = .flat_normal,
                .data = .{ .decoded_blob = .{ .format = .r8g8b8a8_srgb, .width = 1, .height = 1, .blob = &flat_normal } },
            },
        }, "flat_normal");

        // Paladin textures, decoded from the gltf
        var gltf_textures: [3]runtime.Texture = undefined;
        for (0..gltf_textures.len) |i| {
            gltf_textures[i] = (try runtime.Texture.fromGltf(gpa, init.io, gltf_data, @intCast(i), null)) orelse return error.GltfTextureMissing;
        }
        defer for (&gltf_textures) |*t| t.deinit(gpa);

        var gltf_gids: [3]runtime.AssetSystem.Gid = undefined;
        for (0..gltf_textures.len) |i| {
            var name_buf: [64]u8 = undefined;
            const name = try std.fmt.bufPrint(&name_buf, "gltf_texture_{d}", .{i});
            gltf_gids[i] = try ingestAndFinalize(&asset_system, .{
                .texture = .{
                    .default_image = .white,
                    .data = .{ .decoded_blob = .{
                        .format = .r8g8b8a8_srgb,
                        .width = gltf_textures[i].width,
                        .height = gltf_textures[i].height,
                        .blob = gltf_textures[i].pixels,
                    } },
                },
            }, name);
        }

        // one sampled texture per raw texture, carrying its gltf sampler
        _ = try ingestAndFinalize(&asset_system, .{ .sampled_texture = .{ .texture = white_gid, .sampler = .{} } }, "sampled_white");
        _ = try ingestAndFinalize(&asset_system, .{ .sampled_texture = .{ .texture = flat_normal_gid, .sampler = .{} } }, "sampled_flat_normal");
        for (0..gltf_textures.len) |i| {
            var name_buf: [64]u8 = undefined;
            const name = try std.fmt.bufPrint(&name_buf, "sampled_gltf_texture_{d}", .{i});
            _ = try ingestAndFinalize(&asset_system, .{ .sampled_texture = .{ .texture = gltf_gids[i], .sampler = gltf_textures[i].sampler } }, name);
        }

        // geometry + mesh assets for the two rendered gltf primitives; meshes
        // get soft deps on their geometries via MeshRegistry.ingest
        const gltf_result0 = try runtime.MeshIO.fromGltfPrimitive(gpa, gltf_data, 0, 0);
        defer gpa.free(gltf_result0.name);
        defer gltf_result0.mesh_io.deinit(gpa);

        const gltf_result1 = try runtime.MeshIO.fromGltfPrimitive(gpa, gltf_data, 1, 0);
        defer gpa.free(gltf_result1.name);
        defer gltf_result1.mesh_io.deinit(gpa);

        const body_mesh = try runtime.Mesh.init(gpa, gltf_result1.mesh_io.source);
        defer body_mesh.deinit(gpa);

        const helmet_mesh = try runtime.Mesh.init(gpa, gltf_result0.mesh_io.source);
        defer helmet_mesh.deinit(gpa);

        const body_geo_gid = try ingestAndFinalize(&asset_system, .{ .geometry = geometryIngest(body_mesh.lods[0]) }, "body_geometry");
        const helmet_geo_gid = try ingestAndFinalize(&asset_system, .{ .geometry = geometryIngest(helmet_mesh.lods[0]) }, "helmet_geometry");

        var body_lods: [1]runtime.AssetSystem.MeshRegistry.LodEntryBlob = undefined;
        _ = try ingestAndFinalize(&asset_system, .{ .mesh = meshBlob(&body_mesh, body_geo_gid, &body_lods) }, "body_mesh");
        var helmet_lods: [1]runtime.AssetSystem.MeshRegistry.LodEntryBlob = undefined;
        _ = try ingestAndFinalize(&asset_system, .{ .mesh = meshBlob(&helmet_mesh, helmet_geo_gid, &helmet_lods) }, "helmet_mesh");

        // write the manifest
        var manifest_writer_buf: [1024]u8 = undefined;
        var manifest_file_w = try std.Io.Dir.createFile(.cwd(), init.io, asset_dir ++ "/manifest.json", .{});
        defer manifest_file_w.close(init.io);
        var manifest_writer = manifest_file_w.writer(init.io, &manifest_writer_buf);
        var stringify: std.json.Stringify = .{ .writer = &manifest_writer.interface, .options = .{ .whitespace = .indent_2 } };
        try asset_system.save_manifest(&stringify);
        try manifest_writer.flush();
    }

    // Map the material's 5 texture slots to sampled textures in the manifest.
    const slot_fields = [5]u32{ inst.albedo_map, inst.metallic_roughness_map, inst.normal_map, inst.occlusion_map, inst.emissive_map };
    const slot_tags = [_]Slot{ .albedo, .metallic_roughness, .normal, .occlusion, .emissive };
    var slot_bufs: [5][64]u8 = undefined;
    var slot_gids: [5]runtime.AssetSystem.Gid = undefined;
    for (&slot_gids, slot_tags, slot_fields, &slot_bufs) |*gid, slot, field, *buf| {
        gid.* = (asset_system.findEntry(.sampled_texture, slotName(slot, field, buf))) orelse return error.SampledTextureNotFound;
    }

    const body_mesh_gid = asset_system.findEntry(.mesh, "body_mesh") orelse return error.AssetEntryNotFound;
    const helmet_mesh_gid = asset_system.findEntry(.mesh, "helmet_mesh") orelse return error.AssetEntryNotFound;
    const body_geo_gid = asset_system.findEntry(.geometry, "body_geometry") orelse return error.AssetEntryNotFound;
    const helmet_geo_gid = asset_system.findEntry(.geometry, "helmet_geometry") orelse return error.AssetEntryNotFound;

    var asset_cmd_buf = runtime.AssetSystem.CommandBuffer.init(gpa);
    defer asset_cmd_buf.deinit();
    for (slot_gids) |gid| try asset_cmd_buf.request(&asset_system, gid);
    try asset_cmd_buf.request(&asset_system, body_mesh_gid);
    try asset_cmd_buf.request(&asset_system, helmet_mesh_gid);
    // geometry deps on meshes are soft; request them explicitly
    try asset_cmd_buf.request(&asset_system, body_geo_gid);
    try asset_cmd_buf.request(&asset_system, helmet_geo_gid);
    try asset_system.submit(.from(&ctx), &asset_cmd_buf);

    // wait for the first mesh desc / lod upload so the render graph can bind
    // non-empty registry buffers; geometry patches may land a few ticks later,
    // lods with address 0 just don't draw until then
    while (asset_system.mesh_reg.upload_generation == 0 or asset_system.pending_patches.items.len != 0) {
        try ctx.transport.drain(.from(&ctx));
        try asset_system.tick(.from(&ctx));
        std.Thread.yield() catch {};
    }

    // // Let the sampled texture bindings land in the pool before the first frame.
    // while (asset_system.texture_reg.pending_sampled_textures.items.len != 0) {
    //     try ctx.transport.drain(.from(&ctx));
    //     try asset_system.texture_reg.tick(.from(&ctx));
    //     std.Thread.yield() catch {};
    // }

    var mat_handler = runtime.MaterialHandler{};
    defer mat_handler.deinit(gpa, .from(&ctx));

    inst.albedo_map = asset_system.denseIndex(slot_gids[0]);
    inst.metallic_roughness_map = asset_system.denseIndex(slot_gids[1]);
    inst.normal_map = asset_system.denseIndex(slot_gids[2]);
    inst.occlusion_map = asset_system.denseIndex(slot_gids[3]);
    inst.emissive_map = asset_system.denseIndex(slot_gids[4]);
    try mat_handler.append(gpa, 0, inst);
    try mat_handler.flush(.from(&ctx));
    try waitForTick(.from(&ctx), mat_handler.schema_tickets[0]);

    // Give the graph a bindable id for the asset system's texture pool set, on every frame.
    var texture_pool_sets: [base.Ctx.frames_in_flight]u64 = undefined;
    for (0..base.Ctx.frames_in_flight) |i| {
        texture_pool_sets[i] = try ctx.descriptor_pools[i].registerExternalSet(gpa, asset_system.texture_reg.texture_pool.set);
    }

    var vis = try runtime.Visbuffer.init(.from(&ctx), render_extent);
    defer vis.deinit(.from(&ctx));

    var pbr = try runtime.PbrShading.init(.from(&ctx), vis.set_layout, asset_system.texture_reg.texture_pool.set_layout);
    defer pbr.deinit(.from(&ctx));

    var world_instances_buf = try base.Buffer.init(.from(&ctx), .graphics, "world_instances_buf", Culling.world_instance_size, .{ .storage_buffer_bit = true }, .cpu_to_gpu_dynamic);
    defer world_instances_buf.deinit(.from(&ctx));
    {
        const mapped = world_instances_buf.mapped.?;
        @as(*u32, @ptrCast(@alignCast(mapped))).* = 0;
        @memcpy(mapped[4..][0..64], std.mem.asBytes(&mesh_world));
        world_instances_buf.flush(.from(&ctx), 0, Culling.world_instance_size);
    }

    const max_instances = 64;
    var renderables_buf = try base.Buffer.init(.from(&ctx), .graphics, "renderables_buf", @sizeOf(u32) + max_instances * @sizeOf(Culling.RenderableEntry), .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer renderables_buf.deinit(.from(&ctx));

    var draw_cmds_buf = try base.Buffer.init(.from(&ctx), .graphics, "draw_cmds_buf", max_instances * 5 * @sizeOf(u32), .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer draw_cmds_buf.deinit(.from(&ctx));

    // Per-frame arena for skinned vertex cache
    const arena_elements: u32 = 64 * 1024 * 32;
    const arena_buf_size = @sizeOf(u32) * 2 + arena_elements * @sizeOf(u32);
    var arena_buf = try base.Buffer.init(.from(&ctx), .graphics, "skinning_arena", arena_buf_size, .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer arena_buf.deinit(.from(&ctx));

    // Skinned draw cmds (indirect with extra fields), includes u32 count header
    var skinned_draw_cmds_buf = try base.Buffer.init(.from(&ctx), .graphics, "skinned_draw_cmds_buf", @sizeOf(u32) + max_instances * 32, .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    defer skinned_draw_cmds_buf.deinit(.from(&ctx));

    // Skinned world instances (mesh_ix + joint_offset + transform)
    const skinned_instance_count: u32 = 4;
    var skinned_world_instances_buf = try base.Buffer.init(.from(&ctx), .graphics, "skinned_world_instances_buf", skinned_instance_count * Culling.skinned_world_instance_size, .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .cpu_to_gpu_dynamic);
    defer skinned_world_instances_buf.deinit(.from(&ctx));

    var culling = try Culling.init(.from(&ctx));
    defer culling.deinit(.from(&ctx));

    // GRID drawing pipeline
    const fullscreen_vert_spirv = shaders.get(.fullscreen_triangle);
    const fullscreen_vert_mod = try base.ShaderModule.init(.from(&ctx), fullscreen_vert_spirv);
    defer fullscreen_vert_mod.deinit(.from(&ctx));

    const grid_spirv = shaders.get(.grid);
    const grid_mod = try base.ShaderModule.init(.from(&ctx), grid_spirv);
    defer grid_mod.deinit(.from(&ctx));

    const GridPC = struct {
        inv_vp: zm.Mat,
        vp: zm.Mat,
        cam_pos: [3]f32,
        screen: [2]f32,
    };

    var grid_pipeline = try base.GraphicsPipeline.init(.from(&ctx), .{
        .fragment = grid_mod,
        .vertex = fullscreen_vert_mod,
        .push_constant_size = @sizeOf(GridPC),
        .color_attachments = &.{
            base.GraphicsPipeline.ColorAttachment{
                .format = render_format,
                .blend = .{
                    .blend_enable = .true,
                    .src_color_blend_factor = .one,
                    .src_alpha_blend_factor = .one,
                    .dst_color_blend_factor = .one_minus_src_alpha,
                    .dst_alpha_blend_factor = .one_minus_src_alpha,
                    .color_blend_op = .add,
                    .alpha_blend_op = .add,
                    .color_write_mask = .{
                        .r_bit = true,
                        .g_bit = true,
                        .b_bit = true,
                        .a_bit = true,
                    }
                },
            },
        },
        .depth_format = base.Ctx.depth_texture,
    });
    defer grid_pipeline.deinit(.from(&ctx));
    grid_pipeline.depth_test_enable = .true;
    grid_pipeline.depth_compare_op = .less;
    grid_pipeline.depth_write_enable = .true;
    grid_pipeline.cull_mode = .{};

    const aspect = @as(f32, @floatFromInt(ctx.render_extent.width)) / @as(f32, @floatFromInt(ctx.render_extent.height));
    var cam = base.Camera.initLookAt(
        zm.f32x4(0, 0, 2, 1),
        zm.f32x4(0, 0, 0, 1),
        std.math.pi / 4.0,
        aspect,
        0.1,
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

                zgui.text("x: {any}, y: {any}, z: {any}", .{ cam.position[0], cam.position[1], cam.position[2] });
            }
            zgui.end();

            try ctx.transport.drain(.from(&ctx));
            try asset_system.tick(.from(&ctx));

            const frame = ctx.frames[ctx.current_frame];
            const rt = ctx.renderTarget();
            const dt = ctx.depthTarget();

            const anim_time = @mod(@as(f32, @floatCast(base.zglfw.getTime())), anims[1].duration);
            {
                const buf = &joint_matrices_bufs[ctx.current_frame];
                const mats: []zm.Mat = @as([*]zm.Mat, @ptrCast(@alignCast(buf.mapped.?)))[0..skin.skeleton_node_indices.len];
                try anims[1].evaluate(gpa, &skeleton, &skin, anim_time, mats);
                if (!buf.coherent) buf.flush(.from(&ctx), 0, buf.size);
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
                .buffer = asset_system.mesh_reg.current_meshes,
                .offset = 0,
                .size = asset_system.mesh_reg.current_meshes.size,
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
                .buffer = asset_system.mesh_reg.current_lods,
                .offset = 0,
                .size = asset_system.mesh_reg.current_lods.size,
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

            const counters_ref = try rg.addBuffer(.{
                .buffer = vis.counters_buf,
                .offset = 0,
                .size = vis.counters_buf.size,
                .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
                .end_usage = .{ .stage = .{ .compute_shader_bit = true }, .access = .{ .shader_storage_read_bit = true } },
            });
            const offsets_ref = try rg.addBuffer(.{
                .buffer = vis.offsets_buf,
                .offset = 0,
                .size = vis.offsets_buf.size,
                .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
                .end_usage = .{ .stage = .{ .compute_shader_bit = true }, .access = .{ .shader_storage_read_bit = true } },
            });
            const dispatch_ref = try rg.addBuffer(.{
                .buffer = vis.dispatch_buf,
                .offset = 0,
                .size = vis.dispatch_buf.size,
                .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
                .end_usage = .{ .stage = .{ .compute_shader_bit = true, .draw_indirect_bit = true }, .access = .{ .shader_storage_read_bit = true, .indirect_command_read_bit = true } },
            });
            const frag_ids_ref = try rg.addBuffer(.{
                .buffer = vis.frag_ids_buf,
                .offset = 0,
                .size = vis.frag_ids_buf.size,
                .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
                .end_usage = .{ .stage = .{ .compute_shader_bit = true }, .access = .{ .shader_storage_read_bit = true } },
            });
            const instances_ref = try rg.addBuffer(.{
                .buffer = mat_handler.schema_bufs[0],
                .offset = 0,
                .size = mat_handler.schema_bufs[0].size,
                .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
                .end_usage = .{ .stage = .{ .compute_shader_bit = true }, .access = .{ .shader_storage_read_bit = true } },
            });

            const zero_pass = try rg.addTransferPass(.{});
            try zero_pass.clearImage(.{
                .image = rt_ref,
                .color = .{ .float_32 = .{ 36.0/255.0, 36.0/255.0, 36.0/255.0, 1.0 } },
            });
            try zero_pass.fillBuffer(.{ .buffer = renderables_ref, .size = renderables_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = draw_cmds_ref, .size = draw_cmds_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = arena_ref, .size = @sizeOf(u32) }); // reset arena write_offset
            try zero_pass.fillBuffer(.{ .buffer = skinned_draw_cmds_ref, .size = @sizeOf(u32) }); // reset skinned count
            try zero_pass.fillBuffer(.{ .buffer = counters_ref, .size = vis.counters_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = offsets_ref, .size = vis.offsets_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = dispatch_ref, .size = vis.dispatch_buf.size });
            try zero_pass.fillBuffer(.{ .buffer = frag_ids_ref, .size = vis.frag_ids_buf.size });

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
                const body_dense = asset_system.denseIndex(body_mesh_gid);
                const helmet_dense = asset_system.denseIndex(helmet_mesh_gid);

                const mapped = skinned_world_instances_buf.mapped.?;
                const instances = std.mem.bytesAsSlice(u32, mapped[0 .. skinned_instance_count * Culling.skinned_world_instance_size]);
                // mesh_desc_ix = mesh registry slot; both use joint_offset 0
                instances[0] = helmet_dense;
                instances[1] = 0; // joint_offset
                // transform at offset 2 (two u32s skipped)
                @memcpy(std.mem.sliceAsBytes(instances[2..18]), std.mem.asBytes(&mesh_world));
                const base_off = Culling.skinned_world_instance_size;
                const inst2 = std.mem.bytesAsSlice(u32, mapped[base_off..][0..Culling.skinned_world_instance_size]);

                inst2[0] = body_dense;
                inst2[1] = 0; // joint_offset
                @memcpy(std.mem.sliceAsBytes(inst2[2..18]), std.mem.asBytes(&mesh_world));
                if (!skinned_world_instances_buf.coherent)
                    skinned_world_instances_buf.flush(.from(&ctx), 0, skinned_world_instances_buf.size);

                const base_off2 = Culling.skinned_world_instance_size*2;
                const inst3 = std.mem.bytesAsSlice(u32, mapped[base_off2..][0..Culling.skinned_world_instance_size]);

                inst3[0] = helmet_dense;
                inst3[1] = 0; // joint_offset
                @memcpy(std.mem.sliceAsBytes(inst3[2..18]), std.mem.asBytes(&mesh_world2));
                if (!skinned_world_instances_buf.coherent)
                    skinned_world_instances_buf.flush(.from(&ctx), 0, skinned_world_instances_buf.size);

                const base_off3 = Culling.skinned_world_instance_size*3;
                const inst4 = std.mem.bytesAsSlice(u32, mapped[base_off3..][0..Culling.skinned_world_instance_size]);

                inst4[0] = body_dense;
                inst4[1] = 0; // joint_offset
                @memcpy(std.mem.sliceAsBytes(inst4[2..18]), std.mem.asBytes(&mesh_world2));
                if (!skinned_world_instances_buf.coherent)
                    skinned_world_instances_buf.flush(.from(&ctx), 0, skinned_world_instances_buf.size);
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
                .draw_cmds_buf_ref = draw_cmds_ref,
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
                    .load_op = .load,
                },
                .renderables_buf_ref = renderables_ref,
                .draw_cmds_buf_ref = skinned_draw_cmds_ref,
                .joints_ref = joints_ref,
                .arena_ref = arena_ref,
                .max_draw_count = max_instances,
                .pc_buf = &skinned_vb_pc_buf,
            });

            const shading_set = try vis.shadingSet(gpa, .from(&ctx), rt.view);

            var count_pc_buf: [runtime.Visbuffer.count_pc_size]u8 = undefined;
            var offsets_pc_buf: [runtime.Visbuffer.offsets_pc_size]u8 = undefined;
            var fragments_pc_buf: [runtime.Visbuffer.fragments_pc_size]u8 = undefined;
            try vis.process(&rg, .{
                .vis_ref = vis_ref,
                .set_id = shading_set,
                .renderables_buf_ref = renderables_ref,
                .counters_ref = counters_ref,
                .offsets_ref = offsets_ref,
                .dispatch_ref = dispatch_ref,
                .frag_ids_ref = frag_ids_ref,
                .count_pc_buf = &count_pc_buf,
                .offsets_pc_buf = &offsets_pc_buf,
                .fragments_pc_buf = &fragments_pc_buf,
            });

            var pbr_pc_buf: [runtime.PbrShading.pbr_pc_size]u8 = undefined;
            try pbr.shade(&rg, gpa, .from(&ctx), texture_pool_sets[ctx.current_frame], .{
                .screen = .{ render_extent.width, render_extent.height },
                .vis_ref = vis_ref,
                .target_ref = rt_ref,
                .vis_set_id = shading_set,
                .dispatch_ref = dispatch_ref,
                .frag_ids_ref = frag_ids_ref,
                .renderables_ref = renderables_ref,
                .instances_ref = instances_ref,
                .arena_ref = arena_ref,
                .cam_pos = .{ cam.position[0], cam.position[1], cam.position[2] },
                .view_proj = cam.view_projection,
                .lights_address = 0,
                .light_count = 0,
                .pc_buf = &pbr_pc_buf,
            });


            var grid_pc_buf: [base.push_constant.size(GridPC, base.layout.scalar)]u8 = undefined;
            base.push_constant.write(GridPC, &grid_pc_buf, .{
                .inv_vp = zm.inverse(cam.view_projection),
                .vp = cam.view_projection,
                .cam_pos = .{ cam.position[0], cam.position[1], cam.position[2] },
                .screen = .{ render_extent.width, render_extent.height },
            }, base.layout.scalar);

            const grid_pass = try rg.addGraphicsPass(.{
                .pipeline = &grid_pipeline,
                .color_attachments = &.{.{
                    .image = rt_ref,
                    .view = rt.view,
                    .load_op = .load,
                    .store_op = .store,
                }},
                .depth_attachment = .{
                    .image = depth_ref,
                    .view = dt.view,
                    .load_op = .load,
                },
                .render_area = .{
                    .offset = .{ .x = 0, .y = 0 },
                    .extent = render_extent,
                },
            });
            try grid_pass.draw(.{
                .push_constant = &grid_pc_buf,
                .vertex_count = 3,
            });

            try rg.run(.from(&ctx), frame.cmd_buf);

            ctx.end_drawing();

            imgui.render(.from(&ctx), frame.cmd_buf);
        }

        if (try ctx.end_frame(gpa) == .recreated) {}
    }
}
