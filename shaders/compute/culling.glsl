#version 460

#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(push_constant, std430) uniform Push {
    CullTaskDatas task_datas;
    CullTasks tasks;

    CulledDrawCmds indirect_draws;
    DrawIDs draw_ids;

    uint max_draw_count;
};

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= tasks.task_count) return;

    CullTask task = read_cull_task(tasks, gid);
    CullTaskData task_data = read_cull_task_data(task_datas, task.data_id);

    uint slot = atomicAdd(draw_ids.current_size, 1);
    if (slot >= max_draw_count) return;

    VertexData verts = VertexData(task_data.verts);
    uint mat_id = read_mesh_data(verts, task_data.verts_start_offset / 4).material_id;
    write_draw_id(draw_ids, slot, DrawID(verts, task_data.verts_start_offset, mat_id, task_data.transforms, task.transform_offset));

    write_culled_draw_cmd(indirect_draws, slot, CulledDrawCmd(task.vertex_count, 1, task.first_vertex, 0));
}
