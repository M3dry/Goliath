#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "library/data.glsl"

layout(push_constant, std430) uniform PushConstants {
    mat4 vp;
    GeometryBuffer geo;
} pc;

layout(location = 0) out vec2 vUv;

void main() {
    Vertex vert = load_vertex(pc.geo, gl_VertexIndex);

    vUv = vert.uv0;
    gl_Position = pc.vp * vec4(vert.pos, 1.0);
}
