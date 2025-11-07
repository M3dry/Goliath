#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, rgba32ui) readonly uniform uimage2D visbuffer;

layout(buffer_reference, std430) readonly buffer DispatchCommand {
    uint val[5];
};

layout(buffer_reference, std430) readonly buffer FragIDs {
    uint id[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    DispatchCommand dispatch;
    FragIDs frag_ids;
    uint mat_id;
};

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

struct Offsets {
    uint start;
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

Offsets read_mesh_offsets(VertexData verts) {
    Offsets offsets;

    offsets.start = verts.data[0];
    offsets.stride = verts.data[1];
    offsets.material_offset = verts.data[2];
    offsets.indices_offset = verts.data[3];
    offsets.position_offset = verts.data[4];
    offsets.normal_offset = verts.data[5];
    offsets.tangent_offset = verts.data[6];
    offsets.texcoord0_offset = verts.data[7];
    offsets.texcoord1_offset = verts.data[8];
    offsets.texcoord2_offset = verts.data[9];
    offsets.texcoord3_offset = verts.data[10];
    offsets.indexed_tangents = verts.data[11] != 0;

    return offsets;
}

struct MeshData {
    Offsets offsets;
    uint material_id;
    uint vertex_count;
    mat4 transform;
    vec3 min;
    vec3 max;
};

MeshData read_mesh_data(VertexData verts) {
    MeshData data;
    data.offsets = read_mesh_offsets(verts);
    data.material_id = verts.data[12];
    data.vertex_count = verts.data[13];

    for (uint i = 0; i < 16; i++) {
        data.transform[i/4][i%4] = uintBitsToFloat(verts.data[14 + i]);
    }

    data.min.x = uintBitsToFloat(verts.data[14 + 16]);
    data.min.y = uintBitsToFloat(verts.data[14 + 16 + 1]);
    data.min.z = uintBitsToFloat(verts.data[14 + 16 + 2]);

    data.max.x = uintBitsToFloat(verts.data[14 + 16 + 3]);
    data.max.y = uintBitsToFloat(verts.data[14 + 16 + 4]);
    data.max.z = uintBitsToFloat(verts.data[14 + 16 + 5]);

    return data;
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

Vertex load_vertex(VertexData verts, Offsets offs, uint vertex) {
    Vertex vert;

    uint ix = vertex;
    uint start = offs.start/4;
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

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= dispatch.val[4]) return;

    uint frag_id = frag_ids.id[dispatch.val[3] + gid];
    uvec2 frag = uvec2(frag_id / screen.x, frag_id % screen.x);

    uvec4 vis = imageLoad(visbuffer, ivec2(frag));
    VertexData verts = VertexData(vis.xy);
    MeshData mesh_data = read_mesh_data(verts);
    uint primitive_id = vis.z;
    uint bary_ui = vis.w;
    vec3 bary = vec3(unpackHalf2x16(bary_ui), 0.0);
    bary.z = 1.0 - bary.x - bary.y;

    Vertex v1 = load_vertex(verts, mesh_data.offsets, primitive_id * 3);
    Vertex v2 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 1);
    Vertex v3 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 2);

    vec3 normal = bary.x*v1.normal + bary.y*v2.normal + bary.z*v3.normal;

    vec4 color;
    if (mesh_data.material_id == 0u) color = vec4(0.0, 1.0, 0.0, 1.0);
    else if (mesh_data.material_id == -1u) color = vec4(1.0, 0.0, 0.0, 1.0);
    else color = vec4(1.0, 1.0, 1.0, 1.0);

    imageStore(target, ivec2(frag), vec4(normal, 1.0));
}
