struct VertexBuffer {
    float4 positions[];
};

struct PushConstants {
    float4x4 vp;
    uint64_t vertex_buffer_address;
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
    VertexBuffer* vb = (VertexBuffer*)(_pc.vertex_buffer_address);

    const float2 uvs[3] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(0.5, 1.0) };

    float3 pos = vb.positions[vert_id].xyz;

    VSOutput output;
    output.vUv = uvs[vert_id % 3];
    output.position = mul(_pc.vp, float4(pos, 1.0));
    return output;
}

[shader("fragment")]
void fragment(VSOutput input) {
    float4 color = uTexture.Sample(uSampler, input.vUv);
}
