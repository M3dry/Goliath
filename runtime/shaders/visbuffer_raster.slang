import data;

struct PushConstants {
    float4x4 vp;
    RenderableBuffer* renderables;
    DrawCmdBuffer* cmds;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSOutput {
    float4 position : SV_Position;
    nointerpolation uint vis_packed;
};

struct PSOutput {
    uint vis_buffer : SV_Target0;
};

[shader("vertex")]
VSOutput vertex(uint vert_id : SV_VertexID, uint draw_id : SV_DrawIndex) {
    DrawCmd cmd = pc.cmds.cmd[draw_id];
    Renderable rend = pc.renderables.data[cmd.renderable_ix];

    Vertex vert = load_vertex(rend.geometry, vert_id);

    VSOutput output;
    uint primitive_id = vert_id / 3;
    uint draw_id_out = cmd.renderable_ix + 1;
    output.vis_packed = (primitive_id << 14) | draw_id_out;
    output.position = mul(pc.vp, mul(rend.transform, float4(vert.pos, 1.0)));
    return output;
}

[shader("fragment")]
PSOutput fragment(VSOutput input) {
    PSOutput output;
    output.vis_buffer = input.vis_packed;
    return output;
}
