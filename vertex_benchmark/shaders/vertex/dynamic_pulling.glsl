#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "library/dynamic_pulling_data.glsl"

layout(push_constant, scalar) uniform PushConstants {
    mat4 vp;
    GeometryBuffer geo;
} pc;

void main() {
    Vertex vert = load_vertex(pc.geo, gl_VertexIndex);

    gl_Position = pc.vp * vec4(vert.pos, 1.0);
}
