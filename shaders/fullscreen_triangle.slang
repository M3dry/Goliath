struct VSOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertex(uint vert_id : SV_VertexID) {
    float2 pos = float2(0);

    if (vert_id == 0) {
        pos = float2(-1.0, -1.0);
    } else if (vert_id == 1) {
        pos = float2(3.0, -1.0);
    } else if (vert_id == 2) {
        pos = float2(-1.0, 3.0);
    }

    return VSOutput(float4(pos, 0, 1));
}
