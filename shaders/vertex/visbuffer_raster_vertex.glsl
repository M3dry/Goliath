#version 460

#include "library/vertex_data.glsl"
#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout(push_constant, std430) uniform Push {
    DrawIDs draw_ids;

    mat4 vp;
};

layout(location = 0) flat out uint draw_id;
layout(location = 1) flat out uint primitive_id;
layout(location = 2) out vec3 barycentric;

void main() {
    draw_id = gl_DrawID;
    DrawID draw_val = read_draw_id(draw_ids ,draw_id);
    MeshData mesh_data = read_mesh_data(draw_val.group, draw_val.start_offset/4);
    Vertex vert = load_vertex(draw_val.group, mesh_data.offsets, gl_VertexIndex, false);

    gl_Position = vp * read_draw_id_transform(draw_val) * mesh_data.transform * vec4(vert.pos, 1.0);

    primitive_id = gl_VertexIndex / 3;
    barycentric = vec3(0.0);
    barycentric[gl_VertexIndex % 3] = 1;
}
