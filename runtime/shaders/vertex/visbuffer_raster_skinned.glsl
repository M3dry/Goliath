#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_16bit_storage : require

#include "library/data.glsl"

layout(push_constant, scalar) uniform PushConstants {
    mat4 vp;
    Renderables renderables;
    SkinnedDrawCmds cmds;
    JointMatrices joints;
} pc;

layout(location = 0) flat out uint vis_packed;

void main() {
    SkinnedDrawCmd cmd = pc.cmds.cmd[gl_DrawID];
    Renderable rend = pc.renderables.data[cmd.renderable_ix];

    GeometryBuffer original_geo = cmd.original_geometry;

    Vertex vert = load_vertex(original_geo, gl_VertexIndex);

    // Compute skinning matrix
    mat4 skin_mat = vert.weights.x * pc.joints.matrices[cmd.joint_offset + vert.joints.x] +
                    vert.weights.y * pc.joints.matrices[cmd.joint_offset + vert.joints.y] +
                    vert.weights.z * pc.joints.matrices[cmd.joint_offset + vert.joints.z] +
                    vert.weights.w * pc.joints.matrices[cmd.joint_offset + vert.joints.w];

    // Normal matrix for direction vectors
    mat3 normal_mat = transpose(inverse(mat3(skin_mat)));

    // Transform attributes
    vec3 skinned_pos = (skin_mat * vec4(vert.pos, 1.0)).xyz;
    vec3 skinned_normal = normalize(normal_mat * vert.normal);
    vec4 skinned_tangent = vec4(normalize(normal_mat * vert.tangent.xyz), vert.tangent.w);

    // Write skinned attributes to arena (unique vertex index)
    GeometryBuffer target_geo = rend.geometry;
    uint stride = get_stride(target_geo);
    uint base = vert.index * stride;

    target_geo.data[base + target_geo.position_offset + 0] = floatBitsToUint(skinned_pos.x);
    target_geo.data[base + target_geo.position_offset + 1] = floatBitsToUint(skinned_pos.y);
    target_geo.data[base + target_geo.position_offset + 2] = floatBitsToUint(skinned_pos.z);

    if (target_geo.normal_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.normal_offset + 0] = floatBitsToUint(skinned_normal.x);
        target_geo.data[base + target_geo.normal_offset + 1] = floatBitsToUint(skinned_normal.y);
        target_geo.data[base + target_geo.normal_offset + 2] = floatBitsToUint(skinned_normal.z);
    }

    if (target_geo.tangent_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.tangent_offset + 0] = floatBitsToUint(skinned_tangent.x);
        target_geo.data[base + target_geo.tangent_offset + 1] = floatBitsToUint(skinned_tangent.y);
        target_geo.data[base + target_geo.tangent_offset + 2] = floatBitsToUint(skinned_tangent.z);
        target_geo.data[base + target_geo.tangent_offset + 3] = floatBitsToUint(skinned_tangent.w);
    }

    if (target_geo.color0_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.color0_offset + 0] = floatBitsToUint(vert.color.x);
        target_geo.data[base + target_geo.color0_offset + 1] = floatBitsToUint(vert.color.y);
        target_geo.data[base + target_geo.color0_offset + 2] = floatBitsToUint(vert.color.z);
        target_geo.data[base + target_geo.color0_offset + 3] = floatBitsToUint(vert.color.w);
    }

    if (target_geo.texcoord0_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord0_offset + 0] = floatBitsToUint(vert.uv0.x);
        target_geo.data[base + target_geo.texcoord0_offset + 1] = floatBitsToUint(vert.uv0.y);
    }

    if (target_geo.texcoord1_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord1_offset + 0] = floatBitsToUint(vert.uv1.x);
        target_geo.data[base + target_geo.texcoord1_offset + 1] = floatBitsToUint(vert.uv1.y);
    }

    if (target_geo.texcoord2_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord2_offset + 0] = floatBitsToUint(vert.uv2.x);
        target_geo.data[base + target_geo.texcoord2_offset + 1] = floatBitsToUint(vert.uv2.y);
    }

    if (target_geo.texcoord3_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord3_offset + 0] = floatBitsToUint(vert.uv3.x);
        target_geo.data[base + target_geo.texcoord3_offset + 1] = floatBitsToUint(vert.uv3.y);
    }

    uint primitive_id = gl_VertexIndex / 3;
    uint draw_id = cmd.renderable_ix + 1;
    vis_packed = (primitive_id << 14) | draw_id;

    gl_Position = pc.vp * rend.transform * vec4(skinned_pos, 1.0);
}
