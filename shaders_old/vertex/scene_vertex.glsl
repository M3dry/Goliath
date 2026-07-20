#version 460

#include "library/vertex_data.glsl"
#include "library/mesh_data.glsl"

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

layout(location = 0) flat out uint mesh_data_offset;
layout(location = 1) flat out uint primitive_id;
layout(location = 2) out vec3 barycentric;

uint get_mesh_data_offset() {
    return draws.data[gl_DrawID].data[4]/4;
}

mat4 get_mesh_instance_transform() {
    uint offset = draws.data[gl_DrawID].data[5]/4 + gl_InstanceIndex * 16;

    mat4 ret;

    for (uint i = 0; i < 16; i++) {
        ret[i/4][i%4] = uintBitsToFloat(verts.data[offset + i]);
    }

    return ret;
}

void main() {
    MeshData mesh_data = read_mesh_data(verts, get_mesh_data_offset());
    Vertex vert = load_vertex(verts, mesh_data.offsets, gl_VertexIndex, false);
    mat4 instance_transform = get_mesh_instance_transform();

    gl_Position = vp * m * instance_transform * mesh_data.transform * vec4(vert.pos, 1.0);

    mesh_data_offset = get_mesh_data_offset()*4;
    primitive_id = gl_VertexIndex / 3;
    barycentric = vec3(0.0);
    barycentric[gl_VertexIndex % 3] = 1;
}
