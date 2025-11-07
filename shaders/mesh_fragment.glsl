#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

layout(buffer_reference, std430) readonly buffer DrawCommands {
    uint data[];
};

layout(push_constant, std430) uniform Push {
    VertexData verts;
    DrawCommands draws;
    mat4 vp;
    mat4 m;
};

layout(set = 3, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 f_position;
layout(location = 1) in vec3 f_normal;
layout(location = 2) in vec4 f_tangent;
layout(location = 3) in vec2 f_texcoord0;
layout(location = 4) in vec2 f_texcoord1;
layout(location = 5) in vec2 f_texcoord2;
layout(location = 6) in vec2 f_texcoord3;
layout(location = 7) flat in uint mesh_data_offset;
layout(location = 8) flat in uint primitive_id;
layout(location = 9) in vec3 barycentric;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out uvec4 vis_buffer;

uvec2 addToAddr(uvec2 addr, uint offsetBytes) {
    uint lo = addr.x + offsetBytes;
    uint carry = (lo < addr.x) ? 1u : 0u;
    uint hi = addr.y + carry;
    return uvec2(lo, hi);
}

void main() {
    // frag_color = vec4(f_texcoord0, 0.0, 1.0);
    // frag_color = vec4(1.0, 0.0, 0.0, 1.0);

    uvec2 addr = uvec2(verts);
    addr = addToAddr(addr, mesh_data_offset);

    uint bary_ui = packHalf2x16(vec2(barycentric.x, barycentric.y));
    vis_buffer = uvec4(addr.x, addr.y, primitive_id, bary_ui);
}
