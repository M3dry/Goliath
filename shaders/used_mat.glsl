#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(buffer_reference, std430) buffer MatCounters {
    uint counter[];
};

layout(buffer_reference, std430) buffer UsedMats {
    uint data[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    MatCounters mat_counters;
    UsedMats used;
    uint max_material_id;
};

void main() {
    uint mat_id = gl_GlobalInvocationID.x;
    if (mat_id > max_material_id) return;

    uint count = mat_counters.counter[mat_id];
    if (count == 0) return;

    uint write_index = 2*atomicAdd(used.data[0], 1) + 1;
    used.data[write_index] = count;
}
