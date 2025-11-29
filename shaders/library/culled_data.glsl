#ifndef _CULLED_DATA_
#define _CULLED_DATA_

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

struct CulledDrawCmd {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

layout(buffer_reference, std430) readonly buffer CulledDrawCmds {
    uint data[];
};

CulledDrawCmd read_culled_draw_cmd(CulledDrawCmds cmds, uint ix) {
    uint start = ix * 4;
    CulledDrawCmd cmd;

    cmd.vertex_count = cmds.data[start];
    cmd.instance_count = cmds.data[start + 1];
    cmd.first_vertex = cmds.data[start + 2];
    cmd.first_vertex = cmds.data[start + 3];

    return cmd;
}

void write_culled_draw_cmd(CulledDrawCmds cmds, uint ix, CulledDrawCmd cmd) {
    uint start = ix * 4;

    cmds.data[start] = cmd.vertex_count;
    cmds.data[start + 1] = cmd.instance_count;
    cmds.data[start + 2] = cmd.first_vertex;
    cmds.data[start + 3] = cmd.first_vertex;
}

struct DrawID {
    VertexData group;
    uint start_offset;
    uint material_id;
    uvec2 transform_ptr;
    uint transform_offset;
};

layout(buffer_reference, std430) buffer DrawIDs {
    uint current_size;
    uint data[];
};

DrawID read_draw_id(DrawIDs draw_ids, uint ix) {
    uint start = ix * 7;
    DrawID id;

    id.group = VertexData(uvec2(draw_ids.data[start], draw_ids.data[start + 1]));
    id.start_offset = draw_ids.data[start + 2];
    id.material_id = draw_ids.data[start + 3];
    id.transform_ptr.x = draw_ids.data[start + 4];
    id.transform_ptr.y = draw_ids.data[start + 5];
    id.transform_offset = draw_ids.data[start + 6];

    return id;
}

layout(buffer_reference, std430) readonly buffer _Transform {
    uint data[];
};

mat4 read_draw_id_transform(DrawID id) {
    mat4 m;
    _Transform data = _Transform(id.transform_ptr);

    for (uint i = 0; i < 16; i++) {
        m[i/4][i%4] = uintBitsToFloat(data.data[id.transform_offset/4 + i]);
    }

    return m;
}

void write_draw_id(DrawIDs draw_ids, uint ix, DrawID id) {
    uint start = ix * 7;

    draw_ids.data[start] = uvec2(id.group).x;
    draw_ids.data[start + 1] = uvec2(id.group).y;
    draw_ids.data[start + 2] = id.start_offset;
    draw_ids.data[start + 3] = id.material_id;
    draw_ids.data[start + 4] = id.transform_ptr.x;
    draw_ids.data[start + 5] = id.transform_ptr.y;
    draw_ids.data[start + 6] = id.transform_offset;
}

struct CullTaskData {
    uvec2 verts;
    uvec2 transforms;
    uint verts_start_offset;
};

layout(buffer_reference, std430) buffer CullTaskDatas {
    uint data_count;
    uint data[];
};

CullTaskData read_cull_task_data(CullTaskDatas data, uint ix) {
    uint start = ix * 5;
    CullTaskData task_data;

    task_data.verts.x = data.data[start];
    task_data.verts.y = data.data[start + 1];
    task_data.transforms.x = data.data[start + 2];
    task_data.transforms.y = data.data[start + 3];
    task_data.verts_start_offset = data.data[start + 4];

    return task_data;
}

void write_cull_task_data(CullTaskDatas data, uint ix, CullTaskData task_data) {
    uint start = ix * 5;

    data.data[start] = task_data.verts.x;
    data.data[start + 1] = task_data.verts.y;
    data.data[start + 2] = task_data.transforms.x;
    data.data[start + 3] = task_data.transforms.y;
    data.data[start + 4] = task_data.verts_start_offset;
}

struct CullTask {
    uint data_id;
    uint transform_offset;
    uint vertex_count;
    uint first_vertex;
};

layout(buffer_reference, std430) buffer CullTasks {
    uint task_count;
    uint data[];
};

CullTask read_cull_task(CullTasks tasks, uint ix) {
    uint start = ix * 4;
    CullTask task;

    task.data_id = tasks.data[start];
    task.transform_offset = tasks.data[start + 1];
    task.vertex_count = tasks.data[start + 2];
    task.first_vertex = tasks.data[start + 3];

    return task;
}

void write_cull_task(CullTasks tasks, uint ix, CullTask task) {
    uint start = ix * 4;

    tasks.data[start] = task.data_id;
    tasks.data[start + 1] = task.transform_offset;
    tasks.data[start + 2] = task.vertex_count;
    tasks.data[start + 3] = task.first_vertex;
}

#endif
