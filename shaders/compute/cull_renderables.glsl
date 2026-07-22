#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "library/data.glsl"

layout(local_size_x = 32) in;

struct WorldInstance {
    uint mesh_desc_ix;
    mat4 transform;
};

layout(buffer_reference, scalar) readonly buffer WorldInstances {
    WorldInstance instances[];
};

layout(push_constant, scalar) uniform PC {
    mat4 vp;
    WorldInstances world_instances;
    MeshDescs mesh_descs;
    LODEntries lod_entries;
    Renderables renderables;
    DrawCmds draw_cmds;
    uint instance_count;
    uint max_draw_count;
    float screen_width;
    float screen_height;
    float fov_y;
} pc;

bool frustum_test(mat4 vp, vec3 bmin, vec3 bmax) {
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
            vec4 clip = vp * vec4(corners[c], 1.0);
            if (clip[plane] < -clip.w) out_count++;
        }
        if (out_count == 8) return false;
    }
    return true;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= pc.instance_count) return;

    WorldInstance inst = pc.world_instances.instances[gid];
    MeshDesc md = pc.mesh_descs.desc[inst.mesh_desc_ix];

    if (!frustum_test(pc.vp, md.min, md.max)) return;

    float dist = length((inst.transform * vec4(0, 0, 0, 1)).xyz);
    float ppf = 2.0 * tan(pc.fov_y * 0.5) / pc.screen_height;

    uint selected_lod = 0;
    for (uint i = 0; i < md.lod_count; i++) {
        LODEntry le = pc.lod_entries.entry[md.lod_offset + i];
        float pixel_size = le.error_metric / (dist * ppf + 0.001);
        if (pixel_size > 1.0) {
            selected_lod = i;
        } else {
            break;
        }
    }

    LODEntry lod = pc.lod_entries.entry[md.lod_offset + selected_lod];

    uint slot = atomicAdd(pc.renderables.count, 1);
    if (slot >= pc.max_draw_count) return;

    pc.renderables.data[slot] = Renderable(
        inst.transform,
        lod.geometry,
        lod.material_schema,
        lod.material_instance
    );

    pc.draw_cmds.cmd[slot].draw_count = lod.draw_count;
    pc.draw_cmds.cmd[slot].instance_count = 1;
    pc.draw_cmds.cmd[slot].first_vertex = 0;
    pc.draw_cmds.cmd[slot].first_instance = 0;
    pc.draw_cmds.cmd[slot].renderable_ix = slot;
}
