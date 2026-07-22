#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#include "library/data.glsl"

layout(push_constant, scalar) uniform PushConstants {
    mat4 vp;
    Renderables renderables;
    DrawCmds cmds;
} pc;

layout(location = 0) flat out uint vis_packed;

void main() {
    // vis_packed = uint(pc.vp[0]);
    // gl_Position = vec4(0, 0, 0, 1);
    DrawCmd cmd = pc.cmds.cmd[gl_DrawID];
    Renderable rend = pc.renderables.data[cmd.renderable_ix];

    Vertex vert = load_vertex(rend.geometry, gl_VertexIndex);

    uint primitive_id = gl_VertexIndex / 3;
    uint draw_id = cmd.renderable_ix + 1;
    vis_packed = (primitive_id << 14) | draw_id;

    gl_Position = pc.vp * rend.transform * vec4(vert.pos, 1.0);
}
