import data;

// Exclusive prefix sum over up to 1024 schema counters and per-schema
// indirect dispatch entries. Each thread sums four inputs, then subgroup
// scans combine thread-local and subgroup-local prefixes.
groupshared uint subgroup_prefixes[256];

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
    uint lane_idx = WaveGetLaneIndex();
    uint lane_count = WaveGetLaneCount();
    uint wave_idx = tidx / lane_count;
    uint wave_count = (256 + lane_count - 1) / lane_count;

    uint thread_sum = 0;
    for (uint i = 0; i < 4; ++i) {
        uint idx = tidx * 4 + i;
        if (idx < pc.data_size)
            thread_sum += pc.in_data[idx];
    }

    uint thread_prefix = WavePrefixSum(thread_sum);
    uint wave_sum = WaveActiveSum(thread_sum);

    if (lane_idx == 0)
        subgroup_prefixes[wave_idx] = wave_sum;
    GroupMemoryBarrierWithGroupSync();

    // The first subgroup scans the per-subgroup totals. The host selects this
    // shader only when one subgroup has enough lanes to cover all subgroups.
    if (wave_idx == 0) {
        uint value = lane_idx < wave_count ? subgroup_prefixes[lane_idx] : 0;
        uint prefix = WavePrefixSum(value);
        if (lane_idx < wave_count)
            subgroup_prefixes[lane_idx] = prefix;
    }
    GroupMemoryBarrierWithGroupSync();

    uint running = subgroup_prefixes[wave_idx] + thread_prefix;
    for (uint i = 0; i < 4; ++i) {
        uint idx = tidx * 4 + i;
        if (idx < pc.data_size) {
            uint count = pc.in_data[idx];
            pc.out_data[idx] = running;
            pc.dispatch[idx].group_x = count / 256 + uint(count % 256 != 0);
            pc.dispatch[idx].group_y = 1;
            pc.dispatch[idx].group_z = 1;
            pc.dispatch[idx].offset = running;
            pc.dispatch[idx].count = count;
            running += count;
        }
    }
}
