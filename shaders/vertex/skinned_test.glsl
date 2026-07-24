#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "library/data.glsl"

layout(buffer_reference, scalar) readonly buffer JointBuffer {
    mat4 joint_matrices[];
};

layout(push_constant, scalar) uniform PushConstants {
    mat4 vp;
    GeometryBuffer geo;
    JointBuffer joints;
} pc;

layout(location = 0) out vec2 vUv;

void main() {
    Vertex vert = load_vertex(pc.geo, gl_VertexIndex);

    mat4 skin_mat = vert.weights.x * pc.joints.joint_matrices[vert.joints.x] +
                    vert.weights.y * pc.joints.joint_matrices[vert.joints.y] +
                    vert.weights.z * pc.joints.joint_matrices[vert.joints.z] +
                    vert.weights.w * pc.joints.joint_matrices[vert.joints.w];

    vUv = vert.uv0;
    gl_Position = pc.vp * skin_mat * vec4(vert.pos, 1.0);
}
