#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, rgba32ui) readonly uniform uimage2D visbuffer;

layout(buffer_reference, std430) readonly buffer DispatchCommand {
    uvec4 vk_dispatch;
    uint offset;
    uint count;
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    DispatchCommand dispatch;
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

void main() {
    // uvec2 gid = gl_GlobalInvocationID.xy;
    // if (gid.x >= screen.x || gid.y >= screen.y) return;
    //
    // uvec4 vis = imageLoad(visbuffer, ivec2(gid));
    // if (vis.x == 0 && vis.y == 0) return;
    //
    // VertexData verts = VertexData(vis.xy);
    // MeshData mesh_data = read_mesh_data(verts);
    // uint primitive_id = vis.y;
    //
    // vec4 color;
    // if (mesh_data.material_id == 0u) color = vec4(0.0, 1.0, 0.0, 1.0);
    // else if (mesh_data.material_id == -1u) color = vec4(1.0, 0.0, 0.0, 1.0);
    // else color = vec4(1.0, 1.0, 1.0, 1.0);
    //
    // imageStore(target, ivec2(gid), color);
}
