#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

struct DrawCommand {
    // vert count, instance count, first vert, start offset, instance transform offset
    uint data[6];
};

layout(buffer_reference, std430) readonly buffer DrawCommands {
    DrawCommand data[];
};

layout(push_constant, std430) uniform Push {
    VertexData verts;
    DrawCommands draws;
    mat4 vp;
    mat4 m;
};

layout(location = 0) out vec3 f_position;
layout(location = 1) out vec3 f_normal;
layout(location = 2) out vec4 f_tangent;
layout(location = 3) out vec2 f_texcoord0;
layout(location = 4) out vec2 f_texcoord1;
layout(location = 5) out vec2 f_texcoord2;
layout(location = 6) out vec2 f_texcoord3;
layout(location = 7) flat out uint mesh_data_offset;
layout(location = 8) flat out uint primitive_id;
layout(location = 9) out vec3 barycentric;

uint get_mesh_data_offset() {
    return draws.data[gl_DrawID].data[4];
}

mat4 get_mesh_instance_transform() {
    uint offset = draws.data[gl_DrawID].data[5]/4 + gl_InstanceIndex * 16;

    mat4 ret;

    for (uint i = 0; i < 16; i++) {
        ret[i/4][i%4] = uintBitsToFloat(verts.data[offset + i]);
    }

    return ret;
}

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

Offsets read_mesh_offsets(uint start_offset) {
    Offsets offsets;

    offsets.start = verts.data[start_offset];
    offsets.stride = verts.data[start_offset + 1];
    offsets.material_offset = verts.data[start_offset + 2];
    offsets.indices_offset = verts.data[start_offset + 3];
    offsets.position_offset = verts.data[start_offset + 4];
    offsets.normal_offset = verts.data[start_offset + 5];
    offsets.tangent_offset = verts.data[start_offset + 6];
    offsets.texcoord0_offset = verts.data[start_offset + 7];
    offsets.texcoord1_offset = verts.data[start_offset + 8];
    offsets.texcoord2_offset = verts.data[start_offset + 9];
    offsets.texcoord3_offset = verts.data[start_offset + 10];
    offsets.indexed_tangents = verts.data[start_offset + 11] != 0;

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

MeshData read_mesh_data() {
    uint start_offset = get_mesh_data_offset()/4;

    MeshData data;
    data.offsets = read_mesh_offsets(start_offset);
    data.material_id = verts.data[start_offset + 12];
    data.vertex_count = verts.data[start_offset + 13];

    for (uint i = 0; i < 16; i++) {
        data.transform[i/4][i%4] = uintBitsToFloat(verts.data[start_offset + 14 + i]);
    }

    data.min.x = uintBitsToFloat(verts.data[start_offset + 14 + 16]);
    data.min.y = uintBitsToFloat(verts.data[start_offset + 14 + 16 + 1]);
    data.min.z = uintBitsToFloat(verts.data[start_offset + 14 + 16 + 2]);

    data.max.x = uintBitsToFloat(verts.data[start_offset + 14 + 16 + 3]);
    data.max.y = uintBitsToFloat(verts.data[start_offset + 14 + 16 + 4]);
    data.max.z = uintBitsToFloat(verts.data[start_offset + 14 + 16 + 5]);

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

Vertex load_vertex(Offsets offs) {
    Vertex vert;

    uint ix = gl_VertexIndex;
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
    MeshData mesh_data = read_mesh_data();
    Vertex vert = load_vertex(mesh_data.offsets);
    mat4 instance_transform = get_mesh_instance_transform();

    gl_Position = vp * m * instance_transform * mesh_data.transform * vec4(vert.pos, 1.0);
    f_normal = vert.normal;
    f_tangent = vert.tangent;
    f_texcoord0 = vert.texcoord0;
    f_texcoord1 = vert.texcoord1;
    f_texcoord2 = vert.texcoord2;
    f_texcoord3 = vert.texcoord3;

    mesh_data_offset = get_mesh_data_offset();
    primitive_id = gl_VertexIndex / 3;
    barycentric = vec3(0.0);
    barycentric[gl_VertexIndex % 3] = 1;

    // if (gl_VertexIndex == 0) {
    //     gl_Position = vec4(5.0, 0.0, 0.0, 1.0);
    // } else if (gl_VertexIndex == 1) {
    //     gl_Position = vec4(-5.0, 0.0, 0.0, 1.0);
    // } else if (gl_VertexIndex == 2) {
    //     gl_Position = vec4(0.0, 0.0, 5.0, 1.0);
    // } else {
    //     gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    // }
    //
    // gl_Position = vp * m * gl_Position;
}
