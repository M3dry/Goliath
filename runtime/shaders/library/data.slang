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

public struct VisFragment {
    public uint draw_id;
    public uint primitive_id;
}

// visbuffer packing: draw_id (renderable_ix + 1, 0 = empty) in low 14 bits, primitive_id in high 18.
public VisFragment read_vis_fragment(uint packed) {
    VisFragment f;
    f.draw_id = packed & 0x3FFFu;
    f.primitive_id = packed >> 14;
    return f;
}

public struct DispatchEntry {
    public uint group_x;
    public uint group_y;
    public uint group_z;
    public uint offset;
    public uint count;
}

public struct BarycentricDeriv {
    public float3 lambda;
    public float3 ddx;
    public float3 ddy;
};

public BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize) {
    BarycentricDeriv ret;

    float3 invW = 1.0 / float3(pt0.w, pt1.w, pt2.w);

    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    // det is transpose-invariant, so row/col-major constructor ambiguity doesn't matter here.
    float det = determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1));
    float invDet = 1.0 / (abs(det) > 1e-12 ? det : 1e-12);

    ret.ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;

    float ddxSum = dot(ret.ddx, float3(1.0));
    float ddySum = dot(ret.ddy, float3(1.0));

    float2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = 1.0 / interpInvW;

    ret.lambda.x = interpW * (invW.x + deltaVec.x * ret.ddx.x + deltaVec.y * ret.ddy.x);
    ret.lambda.y = interpW * (0.0    + deltaVec.x * ret.ddx.y + deltaVec.y * ret.ddy.y);
    ret.lambda.z = interpW * (0.0    + deltaVec.x * ret.ddx.z + deltaVec.y * ret.ddy.z);

    ret.ddx *= (2.0 / winSize.x);
    ret.ddy *= (2.0 / winSize.y);
    ddxSum  *= (2.0 / winSize.x);
    ddySum  *= (2.0 / winSize.y);

    float interpW_ddx = 1.0 / (interpInvW + ddxSum);
    float interpW_ddy = 1.0 / (interpInvW + ddySum);

    ret.ddx = interpW_ddx * (ret.lambda * interpInvW + ret.ddx) - ret.lambda;
    ret.ddy = interpW_ddy * (ret.lambda * interpInvW + ret.ddy) - ret.lambda;

    ret.lambda = max(ret.lambda, float3(0.0));
    return ret;
}

public struct Interpolated {
    public float value;
    public float ddx;
    public float ddy;
};

public struct Interpolated2 {
    public float2 value;
    public float2 ddx;
    public float2 ddy;
};

public struct Interpolated3 {
    public float3 value;
    public float3 ddx;
    public float3 ddy;
};

public Interpolated interpolate(BarycentricDeriv deriv, float v1, float v2, float v3) {
    float3 mergedV = float3(v1, v2, v3);
    Interpolated ret;

    ret.value = dot(mergedV, deriv.lambda);
    ret.ddx = dot(mergedV, deriv.ddx);
    ret.ddy = dot(mergedV, deriv.ddy);

    return ret;
}

public Interpolated2 interpolate2(BarycentricDeriv deriv, float2 v1, float2 v2, float2 v3) {
    Interpolated interp1 = interpolate(deriv, v1.x, v2.x, v3.x);
    Interpolated interp2 = interpolate(deriv, v1.y, v2.y, v3.y);

    Interpolated2 ret;
    ret.value = float2(interp1.value, interp2.value);
    ret.ddx = float2(interp1.ddx, interp2.ddx);
    ret.ddy = float2(interp1.ddy, interp2.ddy);
    return ret;
}

public Interpolated3 interpolate3(BarycentricDeriv deriv, float3 v1, float3 v2, float3 v3) {
    Interpolated interp1 = interpolate(deriv, v1.x, v2.x, v3.x);
    Interpolated interp2 = interpolate(deriv, v1.y, v2.y, v3.y);
    Interpolated interp3 = interpolate(deriv, v1.z, v2.z, v3.z);

    Interpolated3 ret;
    ret.value = float3(interp1.value, interp2.value, interp3.value);
    ret.ddx = float3(interp1.ddx, interp2.ddx, interp3.ddx);
    ret.ddy = float3(interp1.ddy, interp2.ddy, interp3.ddy);
    return ret;
}

public struct InterpolatedVertex {
    public Interpolated3 pos;
    public Interpolated3 normal;
    public Interpolated3 tangent;
    public float tangent_w;
    public Interpolated2 texcoord0;
    public Interpolated2 texcoord1;
    public Interpolated2 texcoord2;
    public Interpolated2 texcoord3;
};

public InterpolatedVertex interpolate_vertex(uint2 screen, uint2 fragment, float4x4 m, float4x4 vp, Vertex v1, Vertex v2, Vertex v3) {
    float2 fragment_ndc = float2(
        ((float(fragment.x) + 0.5) / float(screen.x)) * 2.0 - 1.0,
        ((float(fragment.y) + 0.5) / float(screen.y)) * 2.0 - 1.0
    );

    float4x4 mvp = mul(vp, m);
    float4 clip0 = mul(mvp, float4(v1.pos, 1.0));
    float4 clip1 = mul(mvp, float4(v2.pos, 1.0));
    float4 clip2 = mul(mvp, float4(v3.pos, 1.0));

    BarycentricDeriv deriv = CalcFullBary(clip0, clip1, clip2, fragment_ndc, float2(float(screen.x), float(screen.y)));

    InterpolatedVertex v;

    float3 p0 = mul(m, float4(v1.pos, 1.0)).xyz;
    float3 p1 = mul(m, float4(v2.pos, 1.0)).xyz;
    float3 p2 = mul(m, float4(v3.pos, 1.0)).xyz;
    v.pos = interpolate3(deriv, p0, p1, p2);

    v.normal = interpolate3(deriv, v1.normal, v2.normal, v3.normal);
    v.tangent = interpolate3(deriv, v1.tangent.xyz, v2.tangent.xyz, v3.tangent.xyz);
    v.tangent_w = v1.tangent.w;
    v.texcoord0 = interpolate2(deriv, v1.uv0, v2.uv0, v3.uv0);
    v.texcoord1 = interpolate2(deriv, v1.uv1, v2.uv1, v3.uv1);
    v.texcoord2 = interpolate2(deriv, v1.uv2, v2.uv2, v3.uv2);
    v.texcoord3 = interpolate2(deriv, v1.uv3, v2.uv3, v3.uv3);

    return v;
}
