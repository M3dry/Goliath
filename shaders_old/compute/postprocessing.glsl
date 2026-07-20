#version 460

#include "library/visbuffer_data.glsl"
#include "library/mesh_data.glsl"

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, r32ui) readonly uniform uimage2D visbuffer;

layout(push_constant, std430) uniform Push {
    uvec2 screen;
};

void main() {
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= screen.x || gid.y >= screen.y) return;

    VisFragment vis = read_vis_fragment(imageLoad(visbuffer, ivec2(gid)).x);
    if (vis.draw_id == 0) return;
}
