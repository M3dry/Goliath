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
    f_normal = vert.normal;
    f_tangent = vert.tangent;
    f_texcoord0 = vert.texcoord0;
    f_texcoord1 = vert.texcoord1;
    f_texcoord2 = vert.texcoord2;
    f_texcoord3 = vert.texcoord3;

    mesh_data_offset = get_mesh_data_offset()*4;
    primitive_id = gl_VertexIndex / 3;
    barycentric = vec3(0.0);
    barycentric[gl_VertexIndex % 3] = 1;
}
