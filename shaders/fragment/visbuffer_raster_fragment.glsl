#version 460

#include "library/visbuffer_data.glsl"
#include "library/vertex_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout(push_constant, std430) uniform Push {
    CulledDrawCmds draws;
    DrawIDs draw_ids;

    mat4 vp;
};

layout(location = 0) flat in uint draw_id;
layout(location = 1) flat in uint primitive_id;

layout(location = 0) out uint vis_buffer;

void main() {
    vis_buffer = write_vis_fragment(VisFragment(draw_id + 1, primitive_id));
}
