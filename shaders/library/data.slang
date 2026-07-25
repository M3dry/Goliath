public struct GeometryBuffer {
    public uint64_t indices;
    public uint stride_indexed_tangents;
    public uint position_offset;
    public uint normal_offset;
    public uint tangent_offset;
    public uint color0_offset;
    public uint texcoord0_offset;
    public uint texcoord1_offset;
    public uint texcoord2_offset;
    public uint texcoord3_offset;
    public uint joints0_offset;
    public uint weights0_offset;
    public uint data[];
}

static const uint STRIDE_MASK = 0x7FFFFFFFu;
static const uint INDEXED_TANGENTS_MASK = 0x80000000u;

public uint get_stride(GeometryBuffer* geo) {
    return geo.stride_indexed_tangents & STRIDE_MASK;
}

public struct Vertex {
    public uint index;
    public float3 pos;
    public float3 normal;
    public float4 tangent;
    public float4 color;
    public float2 uv0;
    public float2 uv1;
    public float2 uv2;
    public float2 uv3;
    public uint4 joints;
    public float4 weights;
}

public Vertex load_vertex(GeometryBuffer* geo, uint vert_ix) {
    uint stride = get_stride(geo);

    uint vert_index = vert_ix;
    if (geo.indices != 0) {
        vert_index = ((uint*)geo.indices)[vert_ix];
    }

    uint base = vert_index * stride;

    Vertex vert;
    vert.index = 0;
    vert.pos = float3(0, 0, 0);
    vert.normal = float3(0, 0, 0);
    vert.tangent = float4(0, 0, 0, 0);
    vert.color = float4(0, 0, 0, 0);
    vert.uv0 = float2(0, 0);
    vert.uv1 = float2(0, 0);
    vert.uv2 = float2(0, 0);
    vert.uv3 = float2(0, 0);
    vert.joints = uint4(0, 0, 0, 0);
    vert.weights = float4(0, 0, 0, 0);

    vert.index = vert_index;
    vert.pos = asfloat(uint3(
        geo.data[base + geo.position_offset],
        geo.data[base + geo.position_offset + 1],
        geo.data[base + geo.position_offset + 2]
    ));

    if (geo.normal_offset != 0xFFFFFFFF) {
        vert.normal = asfloat(uint3(
            geo.data[base + geo.normal_offset],
            geo.data[base + geo.normal_offset + 1],
            geo.data[base + geo.normal_offset + 2]
        ));
    }

    if (geo.texcoord0_offset != 0xFFFFFFFF) {
        vert.uv0 = asfloat(uint2(
            geo.data[base + geo.texcoord0_offset],
            geo.data[base + geo.texcoord0_offset + 1]
        ));
    }

    if (geo.joints0_offset != 0xFFFFFFFF) {
        uint raw0 = geo.data[base + geo.joints0_offset];
        uint raw1 = geo.data[base + geo.joints0_offset + 1];
        vert.joints = uint4(raw0 & 0xFFFF, raw0 >> 16, raw1 & 0xFFFF, raw1 >> 16);
    }

    if (geo.weights0_offset != 0xFFFFFFFF) {
        vert.weights = asfloat(uint4(
            geo.data[base + geo.weights0_offset],
            geo.data[base + geo.weights0_offset + 1],
            geo.data[base + geo.weights0_offset + 2],
            geo.data[base + geo.weights0_offset + 3]
        ));
    }

    return vert;
}
