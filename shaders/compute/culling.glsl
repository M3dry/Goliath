#version 460

#include "library/vertex_data.glsl"
#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

struct IndirectDraw {
    uint vert_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
    uint start_offset;
    uint transform_offset;
};

layout(buffer_reference, std430) readonly buffer IndirectDraws {
    uint cmds[];
};

struct InstancedIndirectDispatch {
    // uint x;
    // uint y;
    // uint z;
    // uvec2 vertex_data;
    // uint verts_offset;
    // uvec2 transforms;
    // uint transforms_offset;
    // uint instance_count;
    uint data[10];
};
//
layout(buffer_reference, std430) readonly buffer InstancedIndirectDispatchCmds {
    uint dispatch_count;
    InstancedIndirectDispatch cmd[];
};

layout(push_constant, std430) uniform Push {
    VertexData group;
    DrawIDs draw_ids;

    IndirectDraws indirect_draws;
    CulledDrawCmds culled_draws;

    InstancedIndirectDispatchCmds instanced_cmds;
    uint indirect_draw_count;
    uint max_draw_id_count;

    mat4 model;
};

IndirectDraw read_draw_cmd(uint ix) {
    uint start = ix * 5;

    IndirectDraw draw;

    draw.vert_count = indirect_draws.cmds[start];
    draw.instance_count = indirect_draws.cmds[start + 1];
    draw.first_vertex = indirect_draws.cmds[start + 2];
    draw.first_instance = indirect_draws.cmds[start + 3];
    draw.start_offset = indirect_draws.cmds[start + 4];
    draw.transform_offset = indirect_draws.cmds[start + 5];

    return draw;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= indirect_draw_count) return;

    IndirectDraw draw_cmd = read_draw_cmd(gid);
    MeshData mesh_data = read_mesh_data(group, draw_cmd.start_offset/4);

    for (uint i = 0; i < draw_cmd.instance_count; i++) {
        uint slot = atomicAdd(draw_ids.current_size, 1);
        if (slot >= max_draw_id_count) return;

    // if (draw_cmd.instance_count != 1) {
    //     uint dispatch_slot = atomicAdd(instanced_cmds.dispatch_count, 1);
    //
    //     instanced_cmds.cmd[dispatch_slot].data[0] = uint(ceil(draw_cmd.instance_count/32.0));
    //     instanced_cmds.cmd[dispatch_slot].data[1] = 1;
    //     instanced_cmds.cmd[dispatch_slot].data[2] = 1;
    //     instanced_cmds.cmd[dispatch_slot].data[3] = uvec2(group).x;
    //     instanced_cmds.cmd[dispatch_slot].data[4] = uvec2(group).y;
    //     instanced_cmds.cmd[dispatch_slot].data[5] = draw_cmd.start_offset;
    //     instanced_cmds.cmd[dispatch_slot].data[6] = uvec2(group).x;
    //     instanced_cmds.cmd[dispatch_slot].data[7] = uvec2(group).y;
    //     instanced_cmds.cmd[dispatch_slot].data[8] = draw_cmd.transform_offset;
    //     instanced_cmds.cmd[dispatch_slot].data[9] = draw_cmd.instance_count;
    //
    //     return;
    // }

        draw_ids.id[slot].group = group;
        draw_ids.id[slot].start_offset = draw_cmd.start_offset;
        draw_ids.id[slot].material_id = mesh_data.material_id;
        if (draw_cmd.transform_offset != -1) {
            mat4 m;

            for (uint u = 0; u < 16; u++) {
                m[u/4][u%4] = uintBitsToFloat(group.data[draw_cmd.transform_offset/4 + 64*i + u]);
            }

            draw_ids.id[slot].model_transform = m;
        } else {
            draw_ids.id[slot].model_transform = model;
        }

        culled_draws.cmd[slot].data[0] = draw_cmd.vert_count;
        culled_draws.cmd[slot].data[1] = 1;
        culled_draws.cmd[slot].data[2] = draw_cmd.first_vertex;
        culled_draws.cmd[slot].data[3] = draw_cmd.first_instance;
        culled_draws.cmd[slot].data[4] = slot;
    }
}
