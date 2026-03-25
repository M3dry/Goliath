#version 460

#include "library/culled_data.glsl"
#include "library/visbuffer_data.glsl"
#include "library/mesh_data.glsl"

layout (local_size_x = 3, local_size_y = 3, local_size_z = 1) in;

layout(set = 0, binding = 0, r32ui) readonly uniform uimage2D visbuffer;

layout(buffer_reference, std430) buffer Samples {
    uint ids[9];
};

layout(push_constant, std430) uniform Push {
    uvec2 point;
    uvec2 screen_size;
    Samples samples;

    DrawIDs draw_ids;
};

void main() {
    uvec2 gid = gl_GlobalInvocationID.xy;
    uint write_ix = gid.y * 3 + gid.x;
    ivec2 load_point = ivec2(point.x + gid.x - 1, point.y + gid.y - 1);

    if (load_point.x < 0 || load_point.y < 0 || load_point.x >= screen_size.x || load_point.y >= screen_size.y) {
        samples.ids[write_ix] = -1;
        return;
    }

    VisFragment vis = read_vis_fragment(imageLoad(visbuffer, load_point).x);
    samples.ids[write_ix] = vis.draw_id;
}
