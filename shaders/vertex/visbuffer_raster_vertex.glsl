#version 460

#include "library/vertex_data.glsl"
#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout(push_constant, std430) uniform Push {
    CulledDrawCmds draws;
    DrawIDs draw_ids;

    mat4 vp;
};

layout(location = 0) flat out uint mesh_data_offset;
layout(location = 1) flat out uvec2 verts;
layout(location = 2) flat out uint primitive_id;
layout(location = 3) out vec3 barycentric;

DrawID get_draw_id() {
    return draw_ids.id[draws.cmd[gl_DrawID].data[4]];
}

void main() {
    DrawID draw_id = get_draw_id();
    verts = uvec2(draw_id.group);

    MeshData mesh_data = read_mesh_data(draw_id.group, draw_id.start_offset/4);
    Vertex vert = load_vertex(draw_id.group, mesh_data.offsets, gl_VertexIndex, false);

    gl_Position = vp * draw_id.model_transform * mesh_data.transform * vec4(vert.pos, 1.0);

    mesh_data_offset = draw_id.start_offset;
    primitive_id = gl_VertexIndex / 3;
    barycentric = vec3(0.0);
    barycentric[gl_VertexIndex % 3] = 1;
}
