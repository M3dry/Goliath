#ifndef _DATA_
#define _DATA_

layout(buffer_reference, scalar) readonly buffer GeometryBuffer {
    uint stride_indexed_tangents;
    uint position_offset;
    uint normal_offset;
    uint tangent_offset;
    uint texcoord0_offset;
    uint texcoord1_offset;
    uint texcoord2_offset;
    uint texcoord3_offset;
    uint data[];
};

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
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    vec2 uv3;
};

Vertex load_vertex(GeometryBuffer geo, uint vert_ix) {
    Vertex vert = Vertex(vert_ix, vec3(0xFFFFFFFF), vec3(0xFFFFFFFF), vec4(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF), vec2(0xFFFFFFFF));

    uint stride = get_stride(geo);
    bool indexed_tangents = tangents_indexed(geo);
    if (geo.position_offset != 0) {
        vert.index = geo.data[vert_ix];
    }

    uint base = vert.index * stride;

    vert.pos = vec3(
        uintBitsToFloat(geo.data[base + geo.position_offset]),
        uintBitsToFloat(geo.data[base + geo.position_offset + 1]),
        uintBitsToFloat(geo.data[base + geo.position_offset + 2])
    );

    if (geo.normal_offset != 0xFFFFFFFF) {
        vert.normal = vec3(
            uintBitsToFloat(geo.data[base + geo.normal_offset]),
            uintBitsToFloat(geo.data[base + geo.normal_offset + 1]),
            uintBitsToFloat(geo.data[base + geo.normal_offset + 2])
        );
    }

    if (geo.tangent_offset != 0xFFFFFFFF) {
        vert.tangent = vec4(
            uintBitsToFloat(geo.data[base + geo.tangent_offset]),
            uintBitsToFloat(geo.data[base + geo.tangent_offset + 1]),
            uintBitsToFloat(geo.data[base + geo.tangent_offset + 2]),
            uintBitsToFloat(geo.data[base + geo.tangent_offset + 3])
        );
    }

    if (geo.texcoord0_offset != 0xFFFFFFFF) {
        vert.uv0 = vec2(
            uintBitsToFloat(geo.data[base + geo.texcoord0_offset]),
            uintBitsToFloat(geo.data[base + geo.texcoord0_offset + 1])
        );
    }

    if (geo.texcoord1_offset != 0xFFFFFFFF) {
        vert.uv1 = vec2(
            uintBitsToFloat(geo.data[base + geo.texcoord1_offset]),
            uintBitsToFloat(geo.data[base + geo.texcoord1_offset + 1])
        );
    }

    if (geo.texcoord2_offset != 0xFFFFFFFF) {
        vert.uv2 = vec2(
            uintBitsToFloat(geo.data[base + geo.texcoord2_offset]),
            uintBitsToFloat(geo.data[base + geo.texcoord2_offset + 1])
        );
    }

    if (geo.texcoord3_offset != 0xFFFFFFFF) {
        vert.uv3 = vec2(
            uintBitsToFloat(geo.data[base + geo.texcoord3_offset]),
            uintBitsToFloat(geo.data[base + geo.texcoord3_offset + 1])
        );
    }

    return vert;
}

#endif
