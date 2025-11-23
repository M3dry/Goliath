#version 460

#include "library/mesh_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

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

layout (set = 3, binding = 0) uniform sampler2D textures[];

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, rgba32ui) readonly uniform uimage2D visbuffer;

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= dispatch.val[4]) return;

    uint frag_id = frag_ids.id[dispatch.val[3] + gid];
    uvec2 frag = uvec2(frag_id / screen.x, frag_id % screen.x);
    if (frag.x >= screen.x || frag.y >= screen.y)  return;

    uvec4 vis = imageLoad(visbuffer, ivec2(frag));
    if (vis.x == 0 && vis.y == 0) return;

    VertexData verts = VertexData(vis.xy);
    MeshData mesh_data = read_mesh_data(verts, 0);
    uint primitive_id = vis.z;
    uint bary_ui = vis.w;
    vec3 bary = vec3(unpackHalf2x16(bary_ui), 0.0);
    bary.z = 1.0 - bary.x - bary.y;

    Vertex v1 = load_vertex(verts, mesh_data.offsets, primitive_id * 3, true);
    Vertex v2 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 1, true);
    Vertex v3 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 2, true);

    vec3 normal = bary.x*v1.normal + bary.y*v2.normal + bary.z*v3.normal;

    imageStore(target, ivec2(frag), vec4(normal, 1.0));
}
