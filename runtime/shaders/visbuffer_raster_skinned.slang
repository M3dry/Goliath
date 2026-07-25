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
    nointerpolation uint vis_packed : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertex(uint vert_id : SV_VertexID, uint draw_id : SV_DrawIndex) {
    SkinnedDrawCmd cmd = pc.cmds.cmd[draw_id];
    Renderable rend = pc.renderables.data[cmd.renderable_ix];

    Vertex vert = load_vertex(rend.geometry, vert_id);

    uint4 joints_raw = vert.joints;
    float4 weights = vert.weights;
    uint joint_base = cmd.joint_offset / uint(sizeof(float4x4));

    float4 pos = 0.0;
    [unroll] for (int i = 0; i < 4; i++) {
        float4x4 joint_mat = pc.joints[joint_base + joints_raw[i]];
        pos += weights[i] * mul(joint_mat, float4(vert.pos, 1.0));
    }

    VSOutput output;
    uint primitive_id = vert_id / 3;
    uint draw_id_out = cmd.renderable_ix + 1;
    output.vis_packed = (primitive_id << 14) | draw_id_out;
    output.position = mul(pc.vp, mul(rend.transform, pos));
    return output;
}
