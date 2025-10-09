#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer Vertices {
    vec4 data[];
};

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

layout(push_constant, std430) uniform Push {
    Vertices verts;
    vec4 _color;
    mat4 mvp;
};

void main() {
    gl_Position = mvp * verts.data[gl_VertexIndex];
    color = verts.data[gl_VertexIndex + 3];
    if (gl_VertexIndex == 0) {
        uv = vec2(0.5, 1.0);
    } else if (gl_VertexIndex == 1) {
        uv = vec2(1.0, 0.0);
    } else {
        uv = vec2(0.0, 0.0);
    }
}
