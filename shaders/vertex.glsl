#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer Vertices {
    vec3 data[];
};

layout(location = 0) out vec2 uv;

layout(push_constant, std430) uniform Push {
    Vertices verts;
    vec4 color;
};

void main() {
    gl_Position = vec4(verts.data[gl_VertexIndex], 1.0);
    if (gl_VertexIndex == 0) {
        uv = vec2(0.5, 1.0);
    } else if (gl_VertexIndex == 1) {
        uv = vec2(1.0, 0.0);
    } else {
        uv = vec2(0.0, 0.0);
    }
}
