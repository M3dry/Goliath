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

public bool tangents_indexed(GeometryBuffer* geo) {
    return (geo.stride_indexed_tangents & INDEXED_TANGENTS_MASK) != 0;
}

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
    } else {
        vert.normal = asfloat(uint3(0xFFffFFff, 0xFFffFFff, 0xFFffFFff));
    }

    if (geo.tangent_offset != 0xFFFFFFFF) {
        vert.tangent = asfloat(uint4(
            geo.data[base + geo.tangent_offset],
            geo.data[base + geo.tangent_offset + 1],
            geo.data[base + geo.tangent_offset + 2],
            geo.data[base + geo.tangent_offset + 3]
        ));
    } else {
        vert.tangent = asfloat(uint4(0xFFffFFff, 0xFFffFFff, 0xFFffFFff, 0xFFffFFff));
    }

    if (geo.color0_offset != 0xFFFFFFFF) {
        vert.color = asfloat(uint4(
            geo.data[base + geo.color0_offset],
            geo.data[base + geo.color0_offset + 1],
            geo.data[base + geo.color0_offset + 2],
            geo.data[base + geo.color0_offset + 3]
        ));
    } else {
        vert.color = asfloat(uint4(0xFFffFFff, 0xFFffFFff, 0xFFffFFff, 0xFFffFFff));
    }

    if (geo.texcoord0_offset != 0xFFFFFFFF) {
        vert.uv0 = asfloat(uint2(
            geo.data[base + geo.texcoord0_offset],
            geo.data[base + geo.texcoord0_offset + 1]
        ));
    } else {
        vert.uv0 = asfloat(uint2(0xFFffFFff, 0xFFffFFff));
    }

    if (geo.texcoord1_offset != 0xFFFFFFFF) {
        vert.uv1 = asfloat(uint2(
            geo.data[base + geo.texcoord1_offset],
            geo.data[base + geo.texcoord1_offset + 1]
        ));
    } else {
        vert.uv1 = asfloat(uint2(0xFFffFFff, 0xFFffFFff));
    }

    if (geo.texcoord2_offset != 0xFFFFFFFF) {
        vert.uv2 = asfloat(uint2(
            geo.data[base + geo.texcoord2_offset],
            geo.data[base + geo.texcoord2_offset + 1]
        ));
    } else {
        vert.uv2 = asfloat(uint2(0xFFffFFff, 0xFFffFFff));
    }

    if (geo.texcoord3_offset != 0xFFFFFFFF) {
        vert.uv3 = asfloat(uint2(
            geo.data[base + geo.texcoord3_offset],
            geo.data[base + geo.texcoord3_offset + 1]
        ));
    } else {
        vert.uv3 = asfloat(uint2(0xFFffFFff, 0xFFffFFff));
    }

    if (geo.joints0_offset != 0xFFFFFFFF) {
        uint raw0 = geo.data[base + geo.joints0_offset];
        uint raw1 = geo.data[base + geo.joints0_offset + 1];
        vert.joints = uint4(raw0 & 0xFFFF, raw0 >> 16, raw1 & 0xFFFF, raw1 >> 16);
    } else {
        vert.joints = uint4(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);
    }

    if (geo.weights0_offset != 0xFFFFFFFF) {
        vert.weights = asfloat(uint4(
            geo.data[base + geo.weights0_offset],
            geo.data[base + geo.weights0_offset + 1],
            geo.data[base + geo.weights0_offset + 2],
            geo.data[base + geo.weights0_offset + 3]
        ));
    } else {
        vert.weights = asfloat(uint4(0xFFffFFff, 0xFFffFFff, 0xFFffFFff, 0xFFffFFff));
    }

    return vert;
}

public struct LODEntry {
    public GeometryBuffer* geometry;
    public uint vertex_count;
    public uint draw_count;
    public uint material_schema;
    public uint material_instance;
    public float error_metric;
    public uint _padding;
}

public struct MeshDesc {
    public uint lod_offset;
    public uint lod_count;
    public float3 min;
    public float3 max;
}

public struct Renderable {
    public float4x4 transform;
    public GeometryBuffer* geometry;
    public uint material_schema;
    public uint material_instance;
}

public struct RenderableBuffer {
    public uint count;
    public Renderable data[];
}

public struct DrawCmd {
    public uint draw_count;
    public uint instance_count;
    public uint first_vertex;
    public uint first_instance;
    public uint renderable_ix;
}

public struct DrawCmdBuffer {
    public uint count;
    public DrawCmd cmd[];
}

public struct SkinnedDrawCmd {
    public uint draw_count;
    public uint instance_count;
    public uint first_vertex;
    public uint first_instance;
    public uint renderable_ix;
    public uint joint_offset;
    public GeometryBuffer* original_geometry;
}

public struct SkinnedDrawCmdBuffer {
    public uint count;
    public SkinnedDrawCmd cmd[];
}

public struct SkinningArena {
    public uint write_offset;
    public uint data[];
}

public struct WorldInstance {
    public uint mesh_desc_ix;
    public float4x4 transform;
}
