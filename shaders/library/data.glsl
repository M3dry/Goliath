#ifndef _DATA_
#define _DATA_

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

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
    uint joints0_offset;
    uint weights0_offset;
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
    u16vec4 joints;
    vec4 weights;
};

Vertex load_vertex(GeometryBuffer geo, uint vert_ix) {
    Vertex vert = Vertex(vert_ix, vec3(0xFFFFFFFF), vec3(0xFFFFFFFF), vec4(0xFFFFFFFF), vec4(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), u16vec4(0xFFFF), vec4(0xFFFFFFFF));

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

    if (geo.joints0_offset != 0xFFFFFFFF) {
        uint raw0 = geo.data[base + geo.joints0_offset];
        uint raw1 = geo.data[base + geo.joints0_offset + 1];
        vert.joints = u16vec4(uint16_t(raw0), uint16_t(raw0 >> 16u), uint16_t(raw1), uint16_t(raw1 >> 16u));
    }

    if (geo.weights0_offset != 0xFFFFFFFF) {
        vert.weights = uintBitsToFloat(uvec4(
            geo.data[base + geo.weights0_offset],
            geo.data[base + geo.weights0_offset + 1],
            geo.data[base + geo.weights0_offset + 2],
            geo.data[base + geo.weights0_offset + 3]
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

// 4B alingment, scalar required
struct MeshDesc {
    uint lod_offset; // 0 - 4, 4B alingment
    uint lod_count; // 4 - 8
    vec3 min; // 8 - 20
    vec3 max; // 20 - 32
}; // no padding since 32/4 = 8

layout(buffer_reference, scalar) readonly buffer MeshDescs {
    MeshDesc desc[];
};

// 8B alignment, scalar required
struct Renderable {
    mat4 transform; // 0 - 64, 4B alingment
    GeometryBuffer geometry; // 64 - 72
    uint material_schema; // 72 - 76
    uint material_instance; // 76 - 80
}; // no padding since 80/8 = 10

layout(buffer_reference, scalar) buffer Renderables {
    uint count; // 0 - 4
    Renderable data[]; // 8 - 88 since 8B alignment
};

// 4B alignment
struct DrawCmd {
    uint draw_count; // 0 - 4
    uint instance_count; // 4 - 8
    uint first_vertex; // 8 - 12
    uint first_instance; // 12 - 16
    uint renderable_ix; // 16 - 20
}; // no padding

layout(buffer_reference, scalar) buffer DrawCmds {
    DrawCmd cmd[];
};

#endif
