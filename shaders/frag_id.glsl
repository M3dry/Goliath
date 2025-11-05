#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(buffer_reference, std430) buffer Offsets {
    uint offset[];
};

layout(buffer_reference, std430) buffer FragIds {
    uint frag_id[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    Offsets offsets;
    FragIds frag_ids;
    uint max_material_id;
};

layout(set = 0, binding = 0, rgba32ui) readonly uniform uimage2D visbuffer;

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

struct MeshData {
    // ignore offsets
    uint material_id;
    uint vertex_count;
    mat4 transform;
    vec3 min;
    vec3 max;
};

MeshData read_mesh_data(VertexData verts) {
    MeshData data;
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
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= screen.x || gid.y >= screen.y) return;

    uvec4 vis = imageLoad(visbuffer, ivec2(gid));
    if (vis.x == 0 && vis.y == 0) return;

    VertexData verts = VertexData(vis.xy);
    MeshData mesh_data = read_mesh_data(verts);

    uint mat_id = mesh_data.material_id;
    if (mat_id > max_material_id) return;

    uint write_index = atomicAdd(offsets.offset[mat_id], 1);
    frag_ids.frag_id[write_index] = gid.x * screen.x + gid.y;
}
