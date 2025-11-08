#version 460

#include "library/mesh_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba32ui) readonly uniform uimage2D visbuffer;

layout(buffer_reference, std430) buffer MatCounters {
    uint counter[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    MatCounters mat_counters;
};

void main() {
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= screen.x || gid.y >= screen.y) return;

    uvec4 vis = imageLoad(visbuffer, ivec2(gid));
    if (vis.x == 0 && vis.y == 0) return;

    VertexData verts = VertexData(vis.xy);
    MeshData mesh_data = read_mesh_data(verts, 0);

    mesh_data.material_id;
    atomicAdd(mat_counters.counter[mesh_data.material_id], 1);
}
