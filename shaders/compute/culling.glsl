#version 460

#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(buffer_reference, std430) readonly buffer Transforms {
    uint data[];
};

layout(push_constant, std430) uniform Push {
    CullTaskDatas task_datas;
    CullTasks tasks;

    CulledDrawCmds indirect_draws;
    DrawIDs draw_ids;

    uint max_draw_count;
};

mat4 read_mat4(Transforms data, uint start_offset) {
    mat4 m;

    for (uint i = 0; i < 16; i++) {
        m[i/4][i%4] = uintBitsToFloat(data.data[start_offset + i]);
    }

    return m;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= tasks.task_count) return;

    CullTask task = read_cull_task(tasks, gid);
    CullTaskData task_data = read_cull_task_data(task_datas, task.data_id);

    uint slot = atomicAdd(draw_ids.current_size, 1);
    if (slot >= max_draw_count) return;

    VertexData verts = VertexData(task_data.verts);
    uint mat_id = read_mesh_data(verts, task_data.verts_start_offset / 4).material_id;
    draw_ids.id[slot] = DrawID(verts, task_data.verts_start_offset, mat_id, read_mat4(Transforms(task_data.transforms), task.transform_offset/4));

    write_culled_draw_cmd(indirect_draws, slot, CulledDrawCmd(task.vertex_count, 1, task.first_vertex, 0));
}
