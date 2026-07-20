#version 460

#include "library/vertex_data.glsl"
#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout(push_constant, std430) uniform Push {
    DrawIDs draw_ids;
    CulledDrawCmds draws;

    mat4 vp;
};

layout(location = 0) flat out uint draw_id;
layout(location = 1) flat out uint primitive_id;
layout(location = 2) flat out uvec2 transform_ptr;
layout(location = 3) flat out uint transform_offset;
layout(location = 4) flat out mat4 transform;

void main() {
    draw_id = read_culled_draw_cmd(draws, gl_DrawID).draw_id;
    primitive_id = gl_VertexIndex / 3;

    DrawID draw_val = read_draw_id(draw_ids, draw_id);
    MeshData mesh_data = read_mesh_data(draw_val.group, draw_val.start_offset/4);
    Vertex vert = load_vertex(draw_val.group, mesh_data.offsets, gl_VertexIndex, false);

    transform = read_draw_id_transform(draw_val);
    transform_ptr = draw_val.transform_ptr;
    transform_offset = draw_val.transform_offset;
    gl_Position = vp * transform * mesh_data.transform * vec4(vert.pos, 1.0);
}
