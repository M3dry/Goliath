import data;

struct PushConstants {
    float4x4 vp;
    RenderableBuffer* renderables;
    SkinnedDrawCmdBuffer* cmds;
    float4x4* joints;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSOutput {
    float4 position : SV_Position;
    nointerpolation uint vis_packed;
};

[shader("vertex")]
VSOutput vertex(uint vert_id : SV_VertexID, uint draw_id : SV_DrawIndex) {
    SkinnedDrawCmd cmd = pc.cmds.cmd[draw_id];
    Renderable rend = pc.renderables.data[cmd.renderable_ix];

    // Skin from the original geometry, write the result into the arena cache
    // (rend.geometry points at the per-draw arena region for this frame).
    Vertex vert = load_vertex(cmd.original_geometry, vert_id);

    uint joint_base = cmd.joint_offset / uint(sizeof(float4x4));

    float4 pos = 0.0;
    float3 normal = 0.0;
    float3 tangent = 0.0;
    [unroll] for (int i = 0; i < 4; i++) {
        float4x4 joint_mat = pc.joints[joint_base + vert.joints[i]];
        pos += vert.weights[i] * mul(joint_mat, float4(vert.pos, 1.0));
        normal += vert.weights[i] * mul(joint_mat, float4(vert.normal, 0.0)).xyz;
        tangent += vert.weights[i] * mul(joint_mat, float4(vert.tangent.xyz, 0.0)).xyz;
    }

    float3 skinned_pos = pos.xyz;
    float3 skinned_normal = normalize(normal);
    float4 skinned_tangent = float4(normalize(tangent), vert.tangent.w);

    // Write skinned attributes into the arena at the unique vertex index.
    GeometryBuffer* target_geo = rend.geometry;
    uint stride = get_stride(target_geo);
    uint base = vert.index * stride;

    target_geo.data[base + target_geo.position_offset + 0] = asuint(skinned_pos.x);
    target_geo.data[base + target_geo.position_offset + 1] = asuint(skinned_pos.y);
    target_geo.data[base + target_geo.position_offset + 2] = asuint(skinned_pos.z);

    if (target_geo.normal_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.normal_offset + 0] = asuint(skinned_normal.x);
        target_geo.data[base + target_geo.normal_offset + 1] = asuint(skinned_normal.y);
        target_geo.data[base + target_geo.normal_offset + 2] = asuint(skinned_normal.z);
    }

    if (target_geo.tangent_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.tangent_offset + 0] = asuint(skinned_tangent.x);
        target_geo.data[base + target_geo.tangent_offset + 1] = asuint(skinned_tangent.y);
        target_geo.data[base + target_geo.tangent_offset + 2] = asuint(skinned_tangent.z);
        target_geo.data[base + target_geo.tangent_offset + 3] = asuint(skinned_tangent.w);
    }

    if (target_geo.color0_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.color0_offset + 0] = asuint(vert.color.x);
        target_geo.data[base + target_geo.color0_offset + 1] = asuint(vert.color.y);
        target_geo.data[base + target_geo.color0_offset + 2] = asuint(vert.color.z);
        target_geo.data[base + target_geo.color0_offset + 3] = asuint(vert.color.w);
    }

    if (target_geo.texcoord0_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord0_offset + 0] = asuint(vert.uv0.x);
        target_geo.data[base + target_geo.texcoord0_offset + 1] = asuint(vert.uv0.y);
    }

    if (target_geo.texcoord1_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord1_offset + 0] = asuint(vert.uv1.x);
        target_geo.data[base + target_geo.texcoord1_offset + 1] = asuint(vert.uv1.y);
    }

    if (target_geo.texcoord2_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord2_offset + 0] = asuint(vert.uv2.x);
        target_geo.data[base + target_geo.texcoord2_offset + 1] = asuint(vert.uv2.y);
    }

    if (target_geo.texcoord3_offset != 0xFFFFFFFF) {
        target_geo.data[base + target_geo.texcoord3_offset + 0] = asuint(vert.uv3.x);
        target_geo.data[base + target_geo.texcoord3_offset + 1] = asuint(vert.uv3.y);
    }

    VSOutput output;
    uint primitive_id = vert_id / 3;
    uint draw_id_out = cmd.renderable_ix + 1;
    output.vis_packed = (primitive_id << 14) | draw_id_out;
    output.position = mul(pc.vp, mul(rend.transform, float4(skinned_pos, 1.0)));
    return output;
}
