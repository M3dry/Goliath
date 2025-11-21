#version 460

#include "library/vertex_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout(buffer_reference, std430) readonly buffer DrawCommands {
    uint data[];
};

layout(push_constant, std430) uniform Push {
    VertexData verts;
    DrawCommands draws;
    mat4 vp;
    mat4 m;
};

layout(location = 0) flat in uint mesh_data_offset;
layout(location = 1) flat in uint primitive_id;
layout(location = 2) in vec3 barycentric;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out uvec4 vis_buffer;

uvec2 addToAddr(uvec2 addr, uint offsetBytes) {
    uint lo = addr.x + offsetBytes;
    uint carry = (lo < addr.x) ? 1u : 0u;
    uint hi = addr.y + carry;
    return uvec2(lo, hi);
}

void main() {
    uvec2 addr = uvec2(verts);
    addr = addToAddr(addr, mesh_data_offset);

    uint bary_ui = packHalf2x16(vec2(barycentric.x, barycentric.y));
    vis_buffer = uvec4(addr.x, addr.y, primitive_id, bary_ui);
}
