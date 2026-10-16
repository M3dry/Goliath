import data;

// Exclusive prefix sum over the schema counters + per-schema indirect dispatch
// entries [ceil(count/256), 1, 1, offset, count]. Single workgroup, 256 threads.
groupshared uint shared_sums[256];

struct PC {
    uint* in_data;
    uint* out_data;
    DispatchEntry* dispatch;
    uint data_size;
};

[[vk::push_constant]] ConstantBuffer<PC> pc;

[shader("compute")]
[numthreads(256, 1, 1)]
void compute(uint3 tid : SV_DispatchThreadID) {
    uint tidx = tid.x;
    uint n = pc.data_size;

    uint thread_sum = 0;
    for (uint i = 0; i < 4; ++i) {
        uint idx = tidx * 4 + i;
        if (idx < n)
            thread_sum += pc.in_data[idx];
    }

    shared_sums[tidx] = thread_sum;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1; offset < 256; offset <<= 1) {
        uint idx = (tidx + 1) * offset * 2 - 1;
        if (idx < 256)
            shared_sums[idx] += shared_sums[idx - offset];
        GroupMemoryBarrierWithGroupSync();
    }

    if (tidx == 255)
        shared_sums[255] = 0;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 128; offset >= 1; offset >>= 1) {
        uint idx = (tidx + 1) * offset * 2 - 1;
        if (idx < 256) {
            uint t = shared_sums[idx - offset];
            shared_sums[idx - offset] = shared_sums[idx];
            shared_sums[idx] += t;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    uint running = shared_sums[tidx];
    for (uint i = 0; i < 4; ++i) {
        uint idx = tidx * 4 + i;
        if (idx < n) {
            pc.out_data[idx] = running;
            pc.dispatch[idx].group_x = (pc.in_data[idx] + 255) / 256;
            pc.dispatch[idx].group_y = 1;
            pc.dispatch[idx].group_z = 1;
            pc.dispatch[idx].offset = running;
            pc.dispatch[idx].count = pc.in_data[idx];
            running += pc.in_data[idx];
        }
    }
}
