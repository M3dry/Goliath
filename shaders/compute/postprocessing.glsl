#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#include "library/mesh_data.glsl"

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, rgba32ui) readonly uniform uimage2D visbuffer;

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    VertexData m1;
    VertexData m2;
};

void main() {
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= screen.x || gid.y >= screen.y) return;

    uvec4 vis = imageLoad(visbuffer, ivec2(gid));
    if (vis.x == 0 && vis.y == 0) return;

    VertexData verts = VertexData(vis.xy);
    MeshData mesh_data = read_mesh_data(verts, 0);
    uint primitive_id = vis.y;

    // uvec2 verts_addr = vis.xy;
    // uvec2 addr1 = uvec2(m1);
    // uvec2 addr2 = uvec2(m2);
    // if (addr1.x == verts_addr.x && addr1.y == verts_addr.y) {
    //     imageStore(target, ivec2(gid), vec4(1.0, 0.0, 0.0, 1.0));
    // } else if (addr2.x == verts_addr.x && addr2.y == verts_addr.y) {
    //     imageStore(target, ivec2(gid), vec4(0.0, 1.0, 0.0, 1.0));
    // }

    // vec4 color;
    // if (mesh_data.material_id == 0u) color = vec4(0.0, 1.0, 0.0, 1.0);
    // else if (mesh_data.material_id == -1u) color = vec4(1.0, 0.0, 0.0, 1.0);
    // else color = vec4(1.0, 1.0, 1.0, 1.0);
    //
    // imageStore(target, ivec2(gid), color);
}
