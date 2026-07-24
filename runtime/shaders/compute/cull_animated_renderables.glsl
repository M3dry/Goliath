#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "library/data.glsl"

layout(local_size_x = 32) in;

struct SkinnedWorldInstance {
    uint mesh_desc_ix;
    uint joint_offset;   // byte offset into joint buffer, 0xFFFFFFFF for static
    mat4 transform;
};

layout(buffer_reference, scalar) readonly buffer SkinnedWorldInstances {
    SkinnedWorldInstance instances[];
};

layout(push_constant, scalar) uniform PC {
    mat4 vp;
    SkinnedWorldInstances world_instances;
    MeshDescs mesh_descs;
    LODEntries lod_entries;
    Renderables renderables;
    SkinnedDrawCmds draw_cmds;
    SkinningArena arena;
    JointMatrices joints;
    uint arena_capacity;
    uint instance_count;
    uint max_draw_count;
    float screen_width;
    float screen_height;
    float fov_y;
    vec3 camera_pos;
} pc;

bool frustum_test(mat4 vp, mat4 model, vec3 bmin, vec3 bmax) {
    vec3 corners[8] = vec3[](
        vec3(bmin.x, bmin.y, bmin.z),
        vec3(bmax.x, bmin.y, bmin.z),
        vec3(bmin.x, bmax.y, bmin.z),
        vec3(bmax.x, bmax.y, bmin.z),
        vec3(bmin.x, bmin.y, bmax.z),
        vec3(bmax.x, bmin.y, bmax.z),
        vec3(bmin.x, bmax.y, bmax.z),
        vec3(bmax.x, bmax.y, bmax.z)
    );

    for (int plane = 0; plane < 4; plane++) {
        int out_count = 0;
        for (int c = 0; c < 8; c++) {
            vec4 world = model * vec4(corners[c], 1.0);
            vec4 clip = vp * world;
            if (clip[plane] < -clip.w) out_count++;
        }
        if (out_count == 8) return false;
    }
    return true;
}

const uint ARENA_HEADER_SIZE = 13; // GPUGeometry = indices(2) + 11 u32 fields

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= pc.instance_count) return;

    SkinnedWorldInstance inst = pc.world_instances.instances[gid];
    if (inst.joint_offset == 0xFFFFFFFF) return; // not animated, handled by static pass
    MeshDesc md = pc.mesh_descs.desc[inst.mesh_desc_ix];

    if (!frustum_test(pc.vp, inst.transform, md.min, md.max)) return;

    vec3 center = (inst.transform * vec4(0.5 * (md.min + md.max), 1.0)).xyz;
    float dist = length(center - pc.camera_pos);
    float ppf = 2.0 * tan(pc.fov_y * 0.5) / pc.screen_height;

    uint selected_lod = md.lod_count;
    for (uint i = 0; i < md.lod_count; i++) {
        LODEntry le = pc.lod_entries.entry[md.lod_offset + i];
        if (uint64_t(le.geometry) == 0) continue;

        float pixel_size = le.error_metric / (dist * ppf + 0.001);
        if (pixel_size > 1.0) {
            selected_lod = i;
            break;
        }
    }

    if (selected_lod == md.lod_count) return;

    LODEntry lod = pc.lod_entries.entry[md.lod_offset + selected_lod];
    uint orig_stride = get_stride(lod.geometry);

    // Arena stride excludes joints/weights (2 + 4 u32s) since the skinned VS doesn't write them
    uint arena_stride = orig_stride;
    if (lod.geometry.joints0_offset != 0xFFFFFFFF) arena_stride -= 2;
    if (lod.geometry.weights0_offset != 0xFFFFFFFF) arena_stride -= 4;
    uint indexed_flag = lod.geometry.stride_indexed_tangents & INDEXED_TANGENTS_MASK;
    uint arena_stride_packed = indexed_flag | arena_stride;

    // Allocate arena space: header + vertex_count * arena_stride
    uint alloc_size = ARENA_HEADER_SIZE + lod.vertex_count * arena_stride;
    uint arena_off = atomicAdd(pc.arena.write_offset, alloc_size);
    if (arena_off + alloc_size > pc.arena_capacity) return;

    // Write arena header: copy original GPUGeometry, clear joints/weights offsets.
    // Arena data[] has no index prefix so subtract position_offset from all attribute
    // offsets (only for present attributes to avoid wrapping 0xFFFFFFFF).
    uint header_base = arena_off;
    uint64_t orig_indices = uint64_t(lod.geometry.indices);
    uint pos_off = lod.geometry.position_offset;
    pc.arena.data[header_base + 0] = uint(orig_indices & 0xFFFFFFFFu);
    pc.arena.data[header_base + 1] = uint(orig_indices >> 32u);
    pc.arena.data[header_base + 2] = arena_stride_packed;
    pc.arena.data[header_base + 3] = 0; // position_offset in arena is always 0
    pc.arena.data[header_base + 4] = lod.geometry.normal_offset != 0xFFFFFFFF ? lod.geometry.normal_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 5] = lod.geometry.tangent_offset != 0xFFFFFFFF ? lod.geometry.tangent_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 6] = lod.geometry.color0_offset != 0xFFFFFFFF ? lod.geometry.color0_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 7] = lod.geometry.texcoord0_offset != 0xFFFFFFFF ? lod.geometry.texcoord0_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 8] = lod.geometry.texcoord1_offset != 0xFFFFFFFF ? lod.geometry.texcoord1_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 9] = lod.geometry.texcoord2_offset != 0xFFFFFFFF ? lod.geometry.texcoord2_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 10] = lod.geometry.texcoord3_offset != 0xFFFFFFFF ? lod.geometry.texcoord3_offset - pos_off : 0xFFFFFFFF;
    pc.arena.data[header_base + 11] = 0xFFFFFFFF; // joints0_offset = null
    pc.arena.data[header_base + 12] = 0xFFFFFFFF; // weights0_offset = null

    // Build arena geometry address: skip arena write_offset (1 u32) + allocated offset in bytes
    uint64_t arena_geo_addr = uint64_t(pc.arena) + 4 + uint64_t(arena_off) * 4;

    uint slot = atomicAdd(pc.renderables.count, 1);
    if (slot >= pc.max_draw_count) {
        atomicMin(pc.renderables.count, pc.max_draw_count);
        return;
    }

    uint skinned_slot = atomicAdd(pc.draw_cmds.count, 1);
    if (skinned_slot >= pc.max_draw_count) {
        atomicMin(pc.draw_cmds.count, pc.max_draw_count);
        return;
    }

    pc.renderables.data[slot] = Renderable(
        inst.transform,
        GeometryBuffer(uint64_t(arena_geo_addr)),
        lod.material_schema,
        lod.material_instance
    );

    pc.draw_cmds.cmd[skinned_slot].draw_count = lod.draw_count;
    pc.draw_cmds.cmd[skinned_slot].instance_count = 1;
    pc.draw_cmds.cmd[skinned_slot].first_vertex = 0;
    pc.draw_cmds.cmd[skinned_slot].first_instance = 0;
    pc.draw_cmds.cmd[skinned_slot].renderable_ix = slot;
    pc.draw_cmds.cmd[skinned_slot].joint_offset = inst.joint_offset;
    pc.draw_cmds.cmd[skinned_slot].original_geometry = lod.geometry;
}
