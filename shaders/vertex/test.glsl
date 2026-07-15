#version 460
#extension GL_EXT_buffer_reference2 : require

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer {
    vec4 positions[];
};

layout(push_constant) uniform PushConstants {
    mat4 vp;
    VertexBuffer vertex_buffer;
} pc;

layout(location = 0) out vec2 vUv;

const vec2 uvs[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.5, 1.0)
);

void main() {
    vec3 pos = pc.vertex_buffer.positions[gl_VertexIndex].xyz;
    gl_Position = pc.vp * vec4(pos, 1.0);
    vUv = uvs[gl_VertexIndex];
}
