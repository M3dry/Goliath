#version 460

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
layout(location = 2) in vec3 barycentric;

layout(location = 0) out uvec4 vis_buffer;

void main() {
    uint bary_ui = packHalf2x16(vec2(barycentric.x, barycentric.y));
    vis_buffer = uvec4(draw_id + 1, 0, primitive_id, bary_ui);
}
