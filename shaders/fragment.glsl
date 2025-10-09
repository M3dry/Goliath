#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer Vertices {
    vec4 data[];
};

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 frag_color;

layout(set = 3, binding = 0) uniform sampler2D textures[];

layout(push_constant, std430) uniform Push {
    Vertices verts;
    vec4 _color;
    mat4 mvp;
};

void main() {
    // frag_color = texture(textures[0], uv);
    frag_color = color;
}
