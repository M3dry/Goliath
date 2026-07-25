import data;

struct PushConstants {
    float4x4 vp;
    GeometryBuffer* geo;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> _pc;

[[vk::binding(0, 0)]] Texture2D uTexture;
[[vk::binding(0, 0)]] SamplerState uSampler;

struct VSOutput {
    float4 position : SV_Position;
    float2 vUv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertex(uint vert_id : SV_VertexID) {
    Vertex vert = load_vertex(_pc.geo, vert_id);

    VSOutput output;
    output.vUv = vert.uv0;
    output.position = mul(_pc.vp, float4(vert.pos, 1.0));
    return output;
}

[shader("fragment")]
float4 fragment(VSOutput input) : SV_Target0 {
    return uTexture.Sample(uSampler, input.vUv);
}
