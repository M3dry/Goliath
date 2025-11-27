#version 460

#include "library/vertex_data.glsl"

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

struct DrawID {
    VertexData group;
    uint start_offset;
    mat4 model_transform;
};

layout(buffer_reference, std430) buffer DrawIDs {
    uint current_size;
    DrawID id[];
};

struct IndirectDraw {
    // uint vert_count;
    // uint instance_count;
    // uint first_vertex;
    // uint first_instance;
    // uint draw_id;
    uint data[5];
};

layout(buffer_reference, std430) buffer IndirectDraws {
    IndirectDraw cmd[];
};

struct InputIndirectDraw {
    // uint vert_count;
    // uint instance_count;
    // uint first_vertex;
    // uint first_instance;
    // uint start_offset;
    uint data[5];
};

layout(buffer_reference, std430) readonly buffer InputIndirectDraws {
    InputIndirectDraw cmd[];
};

layout(push_constant, std430) uniform Push {
    VertexData group;
    DrawIDs draw_ids;
    InputIndirectDraws in_indirect_draws;
    IndirectDraws out_indirect_draws;

    uint in_indirect_draw_count;
    uint max_draw_id_count;

    mat4 model;
};

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= in_indirect_draw_count) return;

    uint slot = atomicAdd(draw_ids.current_size, 1);
    if (slot >= max_draw_id_count) return;

    InputIndirectDraw in_draw_cmd = in_indirect_draws.cmd[gid];

    draw_ids.id[slot].group = group;
    draw_ids.id[slot].start_offset = in_draw_cmd.data[4];
    draw_ids.id[slot].model_transform = model;

    out_indirect_draws.cmd[slot].data[0] = in_draw_cmd.data[0];
    out_indirect_draws.cmd[slot].data[1] = 1;
    out_indirect_draws.cmd[slot].data[2] = in_draw_cmd.data[2];
    out_indirect_draws.cmd[slot].data[3] = in_draw_cmd.data[3];
    out_indirect_draws.cmd[slot].data[4] = slot;
}
