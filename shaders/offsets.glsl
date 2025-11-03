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

layout(push_constant, std430) uniform Push {
    InData in_data;
    OutData out_data;
    uint data_size;
    uint elements_per_thread;
};

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint n = data_size;

    uint start = tid * elements_per_thread;

    // Step 1: compute each thread's sum
    uint thread_sum = 0;
    for (uint i = 0; i < elements_per_thread; ++i) {
        uint idx = start + i;
        if (idx < n)
            thread_sum += in_data.val[idx];
    }

    // Step 2: store each thread's total into shared memory
    shared_sums[tid] = thread_sum;
    barrier();

    // ---- Step 3: Blelloch scan of shared_sums ----
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

    // ---- Step 4: apply scanned block offset ----
    uint running = shared_sums[tid];
    for (uint i = 0; i < elements_per_thread; ++i) {
        uint idx = start + i;
        if (idx < n) {
            out_data.val[idx] = running;
            running += in_data.val[idx];
        }
    }
}
