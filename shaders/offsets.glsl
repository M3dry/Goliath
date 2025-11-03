#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

shared uint partial[1024];

layout(buffer_reference, std430) buffer InData {
    uint val[];
};

layout(buffer_reference, std430) buffer OutData {
    uint val[];
};

layout(push_constant, std430) uniform Push {
    InData in_data;
    OutData out_data;
    uint data_size;
    uint elements_per_thread;
};

void main() {
    uint mat_id = gl_GlobalInvocationID.x;
    if (mat_id >= data_size) return;

    uint tid = gl_LocalInvocationID.x;
    uint base = elements_per_thread * tid;

    uint vals[4];
    for (uint i = 0; i < 4; i++) {
        vals[i] = in_data.val[base + i];
    }

    for (uint i = 1; i < 4; i++) {
        vals[i] += vals[i - 1];
    }

    partial[tid] = vals[3];
    barrier();

    for (uint offset = 1; offset < 256; offset <<= 1) {
        uint tmp = 0;
        if (tid >= offset) tmp = partial[tid - offset];

        barrier();
        partial[tid] += tmp;
        barrier();
    }

    uint prefix = (tid == 0) ? 0 : partial[tid - 1];

    for (uint i = 0; i < 4; i++) {
        vals[i] += prefix;
    }

    for (uint i = 0; i < 4; i++) {
        out_data.val[base + i] = vals[i];
    }
}
