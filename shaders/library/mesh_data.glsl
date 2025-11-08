#ifndef _MESH_DATA_
#define _MESH_DATA_

#include "library/vertex_data.glsl"

struct MeshData {
    Offsets offsets;
    uint material_id;
    uint vertex_count;
    mat4 transform;
    vec3 min;
    vec3 max;
};

MeshData read_mesh_data(VertexData verts, uint start_offset) {
    MeshData data;
    data.offsets = read_offsets(verts, start_offset);
    start_offset += offsets_size;
    data.material_id = verts.data[start_offset];
    data.vertex_count = verts.data[start_offset + 1];

    for (uint i = 0; i < 16; i++) {
        data.transform[i/4][i%4] = uintBitsToFloat(verts.data[start_offset + 2 + i]);
    }

    data.min.x = uintBitsToFloat(verts.data[start_offset + 3 + 16]);
    data.min.y = uintBitsToFloat(verts.data[start_offset + 3 + 16 + 1]);
    data.min.z = uintBitsToFloat(verts.data[start_offset + 3 + 16 + 2]);

    data.max.x = uintBitsToFloat(verts.data[start_offset + 3 + 16 + 3]);
    data.max.y = uintBitsToFloat(verts.data[start_offset + 3 + 16 + 4]);
    data.max.z = uintBitsToFloat(verts.data[start_offset + 3 + 16 + 5]);

    return data;
}

#endif
