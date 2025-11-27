#version 460

#include "library/vertex_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout(push_constant, std430) uniform Push {
    CulledDrawCmds draws;
    DrawIDs draw_ids;

    mat4 vp;
};

layout(location = 0) flat in uint mesh_data_offset;
layout(location = 1) flat in uvec2 verts;
layout(location = 2) flat in uint primitive_id;
layout(location = 3) in vec3 barycentric;

layout(location = 0) out uvec4 vis_buffer;

uvec2 addToAddr(uvec2 addr, uint offsetBytes) {
    uint lo = addr.x + offsetBytes;
    uint carry = (lo < addr.x) ? 1u : 0u;
    uint hi = addr.y + carry;
    return uvec2(lo, hi);
}

void main() {
    uvec2 offset_verts = addToAddr(verts, mesh_data_offset);

    uint bary_ui = packHalf2x16(vec2(barycentric.x, barycentric.y));
    vis_buffer = uvec4(offset_verts.x, offset_verts.y, primitive_id, bary_ui);
}
