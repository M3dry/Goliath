#version 460

#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct IndirectDraw {
    uint vert_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
    uint start_offset;
    uint transform_offset;
};

layout(buffer_reference, std430) readonly buffer IndirectDraws {
    uint data[];
};

layout(buffer_reference, std430) readonly buffer Transforms {
    mat4 transform[];
};

layout(push_constant, std430) uniform Push {
    VertexData group;
    IndirectDraws indirect_draws;

    CullTaskDatas task_datas;
    CullTasks tasks;

    Transforms transforms;
    uint transform_offset;
    uint indirect_draw_count;

    uint max_task_count;
};

IndirectDraw read_indirect_draw(uint ix) {
    uint start = ix * 6;
    IndirectDraw cmd;

    cmd.vert_count = indirect_draws.data[start];
    cmd.instance_count = indirect_draws.data[start + 1];
    cmd.first_vertex = indirect_draws.data[start + 2];
    cmd.first_instance = indirect_draws.data[start + 3];
    cmd.start_offset = indirect_draws.data[start + 4];
    cmd.transform_offset = indirect_draws.data[start + 5];

    return cmd;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= indirect_draw_count) return;

    IndirectDraw draw_cmd = read_indirect_draw(gid);
    MeshData mesh_data = read_mesh_data(group, draw_cmd.start_offset/4);

    uint task_data_slot = atomicAdd(task_datas.data_count, 1);
    uint start_task_slot = atomicAdd(tasks.task_count, draw_cmd.instance_count);

    write_cull_task_data(task_datas, task_data_slot, CullTaskData(uvec2(group), uvec2(transforms), draw_cmd.start_offset));

    for (uint i = 0; i < draw_cmd.instance_count; i++) {
        write_cull_task(tasks, start_task_slot, CullTask(task_data_slot, draw_cmd.transform_offset == -1 ? transform_offset : draw_cmd.transform_offset, draw_cmd.vert_count, draw_cmd.first_vertex));

        start_task_slot++;
    }
}
