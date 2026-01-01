#version 460

#include "library/visbuffer_data.glsl"
#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, r32ui) readonly uniform uimage2D visbuffer;

layout(buffer_reference, std430) buffer MatCounters {
    uint counter[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    MatCounters mat_counters;
    DrawIDs draw_ids;
};

void main() {
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= screen.x || gid.y >= screen.y) return;

    VisFragment vis = read_vis_fragment(imageLoad(visbuffer, ivec2(gid)).x);
    if (vis.draw_id == 0) return;

    atomicAdd(mat_counters.counter[read_draw_id(draw_ids, vis.draw_id - 1).material_id], 1);
}
