#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

struct DrawCommand {
    // vert count, instance count, first vert, first instance
    uint data[5];
};

layout(buffer_reference, std430) readonly buffer DrawCommands {
    DrawCommand data[];
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

layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(f_texcoord0, 0.0, 1.0);
    // frag_color = vec4(1.0, 0.0, 0.0, 1.0);
}
