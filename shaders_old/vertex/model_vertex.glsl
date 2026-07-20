#version 460

#include "library/vertex_data.glsl"
#include "library/mesh_data.glsl"

#extension GL_EXT_buffer_reference : require

struct DrawCommand {
    // vert count, instance count, first vert, start offset
    uint data[5];
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

layout(location = 0) flat out uint mesh_data_offset;
layout(location = 1) flat out uint primitive_id;
layout(location = 2) out vec3 barycentric;

uint get_mesh_data_offset() {
    return draws.data[gl_DrawID].data[4]/4;
}

void main() {
    MeshData mesh_data = read_mesh_data(verts, get_mesh_data_offset());
    Vertex vert = load_vertex(verts, mesh_data.offsets, gl_VertexIndex, false);

    gl_Position = vp * m * mesh_data.transform * vec4(vert.pos, 1.0);

    mesh_data_offset = get_mesh_data_offset()*4;
    primitive_id = gl_VertexIndex / 3;
    barycentric = vec3(0.0);
    barycentric[gl_VertexIndex % 3] = 1;
}
