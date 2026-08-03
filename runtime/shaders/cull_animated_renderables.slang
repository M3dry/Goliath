import data;

struct SkinnedWorldInstance {
    uint mesh_desc_ix;
    uint joint_offset;
    float4x4 transform;
};

struct PC {
    float4x4 vp;
    SkinnedWorldInstance* world_instances;
    MeshDesc* mesh_descs;
    LODEntry* lod_entries;
    RenderableBuffer* renderables;
    SkinnedDrawCmdBuffer* draw_cmds;
    SkinningArena* arena;
    float4x4* joints;
    uint arena_capacity;
    uint instance_count;
    uint max_draw_count;
    float screen_width;
    float screen_height;
    float fov_y;
    float3 camera_pos;
};

[[vk::push_constant]] ConstantBuffer<PC> pc;

bool frustum_test(float4x4 vp, float4x4 model, float3 bmin, float3 bmax) {
    float3 corners[8] = {
        float3(bmin.x, bmin.y, bmin.z),
        float3(bmax.x, bmin.y, bmin.z),
        float3(bmin.x, bmax.y, bmin.z),
        float3(bmax.x, bmax.y, bmin.z),
        float3(bmin.x, bmin.y, bmax.z),
        float3(bmax.x, bmin.y, bmax.z),
        float3(bmin.x, bmax.y, bmax.z),
        float3(bmax.x, bmax.y, bmax.z)
    };

    for (int plane = 0; plane < 4; plane++) {
        int out_count = 0;
        for (int c = 0; c < 8; c++) {
            float4 world = mul(model, float4(corners[c], 1.0));
            float4 clip = mul(vp, world);
            if (clip[plane] < -clip.w) out_count++;
        }
        if (out_count == 8) return false;
    }
    return true;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void compute(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= pc.instance_count) return;

    SkinnedWorldInstance inst = pc.world_instances[tid.x];
    MeshDesc md = pc.mesh_descs[inst.mesh_desc_ix];

    if (!frustum_test(pc.vp, inst.transform, md.min, md.max)) return;

    float3 center = mul(inst.transform, float4(0.5 * (md.min + md.max), 1.0)).xyz;
    float dist = length(center - pc.camera_pos);
    float ppf = 2.0 * tan(pc.fov_y * 0.5) / pc.screen_height;

    uint selected_lod = md.lod_count;
    for (uint i = 0; i < md.lod_count; i++) {
        LODEntry le = pc.lod_entries[md.lod_offset + i];
        if (uint64_t(le.geometry) == 0) continue;

        float pixel_size = le.error_metric / (dist * ppf + 0.001);
        if (pixel_size > 1.0) {
            selected_lod = i;
            break;
        }
    }

    if (selected_lod == md.lod_count) return;

    LODEntry lod = pc.lod_entries[md.lod_offset + selected_lod];

    uint orig_stride = get_stride(lod.geometry);
    uint arena_stride = orig_stride;
    if (lod.geometry.joints0_offset != 0xFFFFFFFF) arena_stride -= 2;
    if (lod.geometry.weights0_offset != 0xFFFFFFFF) arena_stride -= 4;
    uint indexed_flag = lod.geometry.stride_indexed_tangents & 0x80000000u;
    uint arena_stride_packed = indexed_flag | arena_stride;

    // Allocate arena space: GeometryBuffer header + vertex_count * arena_stride.
    // Round the allocation up to an even number of u32s so every region's geometry
    // starts 8-byte aligned (the header's uint64_t indices field requires it).
    const uint ARENA_HEADER_SIZE = 14;
    uint alloc_size = (ARENA_HEADER_SIZE + lod.vertex_count * arena_stride + 1u) & ~1u;
    uint arena_off;
    InterlockedAdd(pc.arena.write_offset, alloc_size, arena_off);
    if (arena_off + alloc_size > pc.arena_capacity) return;

    uint pos_off = lod.geometry.position_offset;
    uint64_t orig_indices = uint64_t(lod.geometry.indices);
    pc.arena.data[arena_off + 1 + 0] = uint(orig_indices & 0xFFFFFFFFu); // indices low
    pc.arena.data[arena_off + 1 + 1] = uint(orig_indices >> 32u); // indices high
    pc.arena.data[arena_off + 1 + 2] = arena_stride_packed;
    pc.arena.data[arena_off + 1 + 3] = 0; // position_offset = 0
    pc.arena.data[arena_off + 1 + 4] = lod.geometry.normal_offset != 0xFFFFFFFF ? lod.geometry.normal_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 5] = lod.geometry.tangent_offset != 0xFFFFFFFF ? lod.geometry.tangent_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 6] = lod.geometry.color0_offset != 0xFFFFFFFF ? lod.geometry.color0_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 7] = lod.geometry.texcoord0_offset != 0xFFFFFFFF ? lod.geometry.texcoord0_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 8] = lod.geometry.texcoord1_offset != 0xFFFFFFFF ? lod.geometry.texcoord1_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 9] = lod.geometry.texcoord2_offset != 0xFFFFFFFF ? lod.geometry.texcoord2_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 10] = lod.geometry.texcoord3_offset != 0xFFFFFFFF ? lod.geometry.texcoord3_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[arena_off + 1 + 11] = 0xFFFFFFFF; // joints0_offset = null
    pc.arena.data[arena_off + 1 + 12] = 0xFFFFFFFF; // weights0_offset = null

    uint64_t arena_geo_addr = uint64_t(pc.arena) + 8 + uint64_t(arena_off) * 4;

    uint slot;
    InterlockedAdd(pc.renderables.count, 1, slot);
    if (slot >= pc.max_draw_count) {
        uint min_val = pc.max_draw_count;
        InterlockedMin(pc.renderables.count, min_val);
        return;
    }

    uint dslot;
    InterlockedAdd(pc.draw_cmds.count, 1, dslot);
    if (dslot >= pc.max_draw_count) {
        uint min_val = pc.max_draw_count;
        InterlockedMin(pc.draw_cmds.count, min_val);
        return;
    }

    pc.renderables.data[slot].transform = inst.transform;
    pc.renderables.data[slot].geometry = (GeometryBuffer*)arena_geo_addr;
    pc.renderables.data[slot].material_schema = lod.material_schema;
    pc.renderables.data[slot].material_instance = lod.material_instance;

    pc.draw_cmds.cmd[dslot].draw_count = lod.draw_count;
    pc.draw_cmds.cmd[dslot].instance_count = 1;
    pc.draw_cmds.cmd[dslot].first_vertex = 0;
    pc.draw_cmds.cmd[dslot].first_instance = 0;
    pc.draw_cmds.cmd[dslot].renderable_ix = slot;
    pc.draw_cmds.cmd[dslot].joint_offset = inst.joint_offset;
    pc.draw_cmds.cmd[dslot].original_geometry = lod.geometry;
}
