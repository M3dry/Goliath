#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

shared uint shared_sums[1024];

layout(buffer_reference, std430) buffer InData {
    uint val[];
};

layout(buffer_reference, std430) buffer OutData {
    uint val[];
};

struct Draw {
    uint vals[5];
};

layout(buffer_reference, std430) buffer Draws {
    Draw draw[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    InData in_data;
    OutData out_data;
    Draws draws;
    uint data_size;
    uint elements_per_thread;
};

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint n = data_size;

    uint start = tid * elements_per_thread;

    uint thread_sum = 0;
    for (uint i = 0; i < elements_per_thread; ++i) {
        uint idx = start + i;
        if (idx < n)
            thread_sum += in_data.val[idx];
    }

    shared_sums[tid] = thread_sum;
    barrier();

    for (uint offset = 1; offset < 256; offset <<= 1) {
        uint idx = (tid + 1) * offset * 2 - 1;
        if (idx < 256)
            shared_sums[idx] += shared_sums[idx - offset];
        barrier();
    }

    if (tid == 255)
        shared_sums[255] = 0;
    barrier();

    for (uint offset = 128; offset >= 1; offset >>= 1) {
        uint idx = (tid + 1) * offset * 2 - 1;
        if (idx < 256) {
            uint t = shared_sums[idx - offset];
            shared_sums[idx - offset] = shared_sums[idx];
            shared_sums[idx] += t;
        }
        barrier();
    }

    uint running = shared_sums[tid];
    for (uint i = 0; i < elements_per_thread; ++i) {
        uint idx = start + i;
        if (idx < n) {
            out_data.val[idx] = running;
            draws.draw[idx].vals[0] = uint(ceil(in_data.val[idx]/16.0));
            draws.draw[idx].vals[1] = 1;
            draws.draw[idx].vals[2] = 1;
            draws.draw[idx].vals[3] = running;
            draws.draw[idx].vals[4] = in_data.val[idx];
            running += in_data.val[idx];
        }
    }
}
