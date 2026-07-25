import data;

struct PushConstants {
    float4x4 vp;
    GeometryBuffer* geo;
    float4x4* joints;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> _pc;

struct VSOutput {
    float4 position : SV_Position;
    float2 vUv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertex(uint vert_id : SV_VertexID) {
    Vertex vert = load_vertex(_pc.geo, vert_id);

    float4x4 skin_mat = vert.weights.x * _pc.joints[vert.joints.x] +
                        vert.weights.y * _pc.joints[vert.joints.y] +
                        vert.weights.z * _pc.joints[vert.joints.z] +
                        vert.weights.w * _pc.joints[vert.joints.w];

    VSOutput output;
    output.vUv = vert.uv0;
    output.position = mul(_pc.vp, mul(skin_mat, float4(vert.pos, 1.0)));
    return output;
}
