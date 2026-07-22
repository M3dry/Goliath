#ifndef _DATA_
#define _DATA_

// 4 alignment
layout(buffer_reference, scalar) readonly buffer GeometryBuffer {
    uint stride_indexed_tangents;
    uint position_offset;
    uint normal_offset;
    uint tangent_offset;
    uint color0_offset;
    uint texcoord0_offset;
    uint texcoord1_offset;
    uint texcoord2_offset;
    uint texcoord3_offset;
    uint data[];
}; // no padding since all elements are uint

const uint STRIDE_MASK = 0x7FFFFFFFu;
const uint INDEXED_TANGENTS_MASK = 0x80000000u;

bool tangents_indexed(GeometryBuffer geo) {
    return (geo.stride_indexed_tangents & INDEXED_TANGENTS_MASK) != 0;
}

uint get_stride(GeometryBuffer geo) {
    return geo.stride_indexed_tangents & STRIDE_MASK;
}

struct Vertex {
    uint index;
    vec3 pos;
    vec3 normal;
    vec4 tangent;
    vec4 color;
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    vec2 uv3;
};

Vertex load_vertex(GeometryBuffer geo, uint vert_ix) {
    Vertex vert = Vertex(vert_ix, vec3(0xFFFFFFFF), vec3(0xFFFFFFFF), vec4(0xFFFFFFFF), vec4(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF));

    uint stride = get_stride(geo);
    bool indexed_tangents = tangents_indexed(geo);
    if (geo.position_offset != 0) {
        vert.index = geo.data[vert_ix];
    }

    uint base = vert.index * stride;

    vert.pos = uintBitsToFloat(uvec3(
        geo.data[base + geo.position_offset],
        geo.data[base + geo.position_offset + 1],
        geo.data[base + geo.position_offset + 2]
    ));

    if (geo.normal_offset != 0xFFFFFFFF) {
        vert.normal = uintBitsToFloat(uvec3(
            geo.data[base + geo.normal_offset],
            geo.data[base + geo.normal_offset + 1],
            (geo.data[base + geo.normal_offset + 2])
        ));
    }

    if (geo.tangent_offset != 0xFFFFFFFF) {
        vert.tangent = uintBitsToFloat(uvec4(
            geo.data[base + geo.tangent_offset],
            geo.data[base + geo.tangent_offset + 1],
            geo.data[base + geo.tangent_offset + 2],
            geo.data[base + geo.tangent_offset + 3]
        ));
    }

    if (geo.color0_offset != 0xFFFFFFFF) {
        vert.color = uintBitsToFloat(uvec4(
            geo.data[base + geo.color0_offset],
            geo.data[base + geo.color0_offset + 1],
            geo.data[base + geo.color0_offset + 2],
            geo.data[base + geo.color0_offset + 3]
        ));
    }

    if (geo.texcoord0_offset != 0xFFFFFFFF) {
        vert.uv0 = uintBitsToFloat(uvec2(
            geo.data[base + geo.texcoord0_offset],
            geo.data[base + geo.texcoord0_offset + 1]
        ));
    }

    if (geo.texcoord1_offset != 0xFFFFFFFF) {
        vert.uv1 = uintBitsToFloat(uvec2(
            geo.data[base + geo.texcoord1_offset],
            geo.data[base + geo.texcoord1_offset + 1]
        ));
    }

    if (geo.texcoord2_offset != 0xFFFFFFFF) {
        vert.uv2 = uintBitsToFloat(uvec2(
            geo.data[base + geo.texcoord2_offset],
            geo.data[base + geo.texcoord2_offset + 1]
        ));
    }

    if (geo.texcoord3_offset != 0xFFFFFFFF) {
        vert.uv3 = uintBitsToFloat(uvec2(
            geo.data[base + geo.texcoord3_offset],
            geo.data[base + geo.texcoord3_offset + 1]
        ));
    }

    return vert;
}

// needs 8B alignment, std430 min
struct LODEntry {
    GeometryBuffer geometry; // 0 - 8, 8B alignment
    uint draw_count; // 8 - 12, 4B alignment
    uint material_schema; // 12 - 16
    uint material_instance; // 16 - 20
    float error_metric; // 20 - 24
}; // no padding since 24/8 = 3

layout(buffer_reference, scalar) readonly buffer LODEntries {
    LODEntry entry[];
};

// 8B alingment, scalar required
struct MeshDesc {
    uint lod_offset; // 0 - 4, 4B alingment
    uint lod_count; // 4 - 8
    LODEntry current_lod; // 8 - 32, 8B alignment
    vec3 min; // 32 - 44, 4B alignment
    vec3 max; // 44 - 56
}; // no padding since 56/8 = 7

layout(buffer_reference, scalar) readonly buffer MeshDescs {
    MeshDesc desc[];
};

// 4B alignment, scalar required
struct InstanceData {
    mat4 transform; // 0 - 64, 4B alingment
    uint mesh_desc_ix; // 64 - 68
    uint material_schema; // 68 - 72
    uint material_instance; // 72 - 76
}; // no padding since 76/4 = 19

layout(buffer_reference, scalar) readonly buffer InstanceDatas {
    InstanceData data[];
};

// 4B alignment
struct DrawCmd = struct {
    uint draw_count; // 0 - 4
    uint instance_count; // 4 - 8
    uint first_vertex; // 8 - 12
    uint first_instance; // 12 - 16
    uint instace_data_ix; // 16 - 20
}; // no padding

layout(buffer_reference, scalar) readonly buffer DrawCmds {
    DrawCmd cmd[];
};

#endif
