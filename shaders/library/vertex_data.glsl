#ifndef _VERTEX_DATA_
#define _VERTEX_DATA_

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

struct Offsets {
    uint start;
    uint relative_start;
    uint stride;
    uint material_offset;
    uint indices_offset;
    uint position_offset;
    uint normal_offset;
    uint tangent_offset;
    uint texcoord0_offset;
    uint texcoord1_offset;
    uint texcoord2_offset;
    uint texcoord3_offset;
    bool indexed_tangents;
};

const uint offsets_size = 12;

const uint STRIDE_MASK = 0x7FFFFFFFu;
const uint INDEXED_TANGENTS_MASK = 0x80000000u;

Offsets read_offsets(const VertexData verts, const uint start_offset) {
    Offsets offsets;

    offsets.start = verts.data[start_offset];
    offsets.relative_start = verts.data[start_offset + 1];
    uint stride = verts.data[start_offset + 2];
    offsets.material_offset = verts.data[start_offset + 3];
    offsets.indices_offset = verts.data[start_offset + 4];
    offsets.position_offset = verts.data[start_offset + 5];
    offsets.normal_offset = verts.data[start_offset + 6];
    offsets.tangent_offset = verts.data[start_offset + 7];
    offsets.texcoord0_offset = verts.data[start_offset + 8];
    offsets.texcoord1_offset = verts.data[start_offset + 9];
    offsets.texcoord2_offset = verts.data[start_offset + 10];
    offsets.texcoord3_offset = verts.data[start_offset + 11];

    offsets.stride = stride & STRIDE_MASK;
    offsets.indexed_tangents = (stride & INDEXED_TANGENTS_MASK) != 0;

    return offsets;
}

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec4 tangent;
    vec2 texcoord0;
    vec2 texcoord1;
    vec2 texcoord2;
    vec2 texcoord3;
};

Vertex load_vertex(const VertexData verts, const Offsets offs, const uint index, const bool relative) {
    Vertex vert;

    uint ix = index;
    uint start = relative ? offs.relative_start/4 : offs.start/4;
    if (offs.indices_offset != uint(-1)) {
        ix = verts.data[start + offs.indices_offset/4 + ix];
    }

    uint stride = offs.stride/4;

    if (offs.position_offset != uint(-1)) {
        vert.pos.x = uintBitsToFloat(verts.data[start + offs.position_offset/4 + ix*stride]);
        vert.pos.y = uintBitsToFloat(verts.data[start + offs.position_offset/4 + ix*stride + 1]);
        vert.pos.z = uintBitsToFloat(verts.data[start + offs.position_offset/4 + ix*stride + 2]);
    }

    if (offs.normal_offset != uint(-1)) {
        vert.normal.x = uintBitsToFloat(verts.data[start + offs.normal_offset/4 + ix*stride]);
        vert.normal.y = uintBitsToFloat(verts.data[start + offs.normal_offset/4 + ix*stride + 1]);
        vert.normal.z = uintBitsToFloat(verts.data[start + offs.normal_offset/4 + ix*stride + 2]);
    }

    if (offs.tangent_offset != uint(-1)) {
        if (offs.indexed_tangents) {
            vert.tangent.x = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*stride]);
            vert.tangent.y = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*stride + 1]);
            vert.tangent.z = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*stride + 2]);
            vert.tangent.w = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*stride + 3]);
        } else {
            vert.tangent.x = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*4]);
            vert.tangent.y = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*4 + 1]);
            vert.tangent.z = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*4 + 2]);
            vert.tangent.w = uintBitsToFloat(verts.data[start + offs.tangent_offset/4 + ix*4 + 3]);
        }
    }

    if (offs.texcoord0_offset != uint(-1)) {
        vert.texcoord0.x = uintBitsToFloat(verts.data[start + offs.texcoord0_offset/4 + ix*stride]);
        vert.texcoord0.y = uintBitsToFloat(verts.data[start + offs.texcoord0_offset/4 + ix*stride + 1]);
    }

    if (offs.texcoord1_offset != uint(-1)) {
        vert.texcoord1.x = uintBitsToFloat(verts.data[start + offs.texcoord1_offset/4 + ix*stride]);
        vert.texcoord1.y = uintBitsToFloat(verts.data[start + offs.texcoord1_offset/4 + ix*stride + 1]);
    }

    if (offs.texcoord2_offset != uint(-1)) {
        vert.texcoord2.x = uintBitsToFloat(verts.data[start + offs.texcoord2_offset/4 + ix*stride]);
        vert.texcoord2.y = uintBitsToFloat(verts.data[start + offs.texcoord2_offset/4 + ix*stride + 1]);
    }

    if (offs.texcoord3_offset != uint(-1)) {
        vert.texcoord3.x = uintBitsToFloat(verts.data[start + offs.texcoord3_offset/4 + ix*stride]);
        vert.texcoord3.y = uintBitsToFloat(verts.data[start + offs.texcoord3_offset/4 + ix*stride + 1]);
    }

    return vert;
}

Vertex interpolate_vertex(const Vertex v1, const Vertex v2, const Vertex v3, const vec3 barycentric) {
    Vertex ret;

    ret.pos = barycentric.x*v1.pos + barycentric.y*v2.pos + barycentric.z*v3.pos;
    ret.normal = barycentric.x*v1.normal + barycentric.y*v2.normal + barycentric.z*v3.normal;
    ret.tangent = barycentric.x*v1.tangent + barycentric.y*v2.tangent + barycentric.z*v3.tangent;
    ret.texcoord0 = barycentric.x*v1.texcoord0 + barycentric.y*v2.texcoord0 + barycentric.z*v3.texcoord0;
    ret.texcoord1 = barycentric.x*v1.texcoord1 + barycentric.y*v2.texcoord1 + barycentric.z*v3.texcoord1;
    ret.texcoord2 = barycentric.x*v2.texcoord2 + barycentric.y*v2.texcoord2 + barycentric.z*v3.texcoord2;
    ret.texcoord3 = barycentric.x*v3.texcoord3 + barycentric.y*v3.texcoord3 + barycentric.z*v3.texcoord3;

    return ret;
}

#endif
