import data;

struct PC {
    uint2 screen;
    RenderableBuffer* renderables;
    uint* offsets;
    uint* frag_ids;
    uint max_schema;
};

[[vk::push_constant]] ConstantBuffer<PC> pc;

[[vk::binding(0, 0)]] RWTexture2D<uint> visbuffer;

uint first_set_lane(uint4 mask) {
    if (mask.x != 0) return firstbitlow(mask.x);
    if (mask.y != 0) return 32 + firstbitlow(mask.y);
    if (mask.z != 0) return 64 + firstbitlow(mask.z);
    return 96 + firstbitlow(mask.w);
}
uint rank_in_mask(uint4 mask, uint lane) {
    uint word = lane >> 5;
    uint bit = lane & 31;

    uint rank = 0;
    if (word >= 1) rank += countbits(mask.x);
    if (word >= 2) rank += countbits(mask.y);
    if (word >= 3) rank += countbits(mask.z);

    uint current_word =
        word == 0 ? mask.x :
        word == 1 ? mask.y :
        word == 2 ? mask.z : mask.w;

    uint lower_bits = (1u << bit) - 1u;
    return rank + countbits(current_word & lower_bits);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void compute(uint3 tid: SV_DispatchThreadID) {
    if (tid.x >= pc.screen.x || tid.y >= pc.screen.y) return;

    VisFragment vis = read_vis_fragment(visbuffer[tid.xy]);
    if (vis.draw_id == 0) return;

    uint schema = pc.renderables.data[vis.draw_id - 1].material_schema;
    if (schema > pc.max_schema) return;

    uint4 mask = WaveMatch(schema);
    uint leader_lane = first_set_lane(mask);

    uint initial_slot;
    uint wave_ix = WaveGetLaneIndex();
    if (wave_ix == leader_lane) {
        uint count = countbits(mask.x)
            + countbits(mask.y)
            + countbits(mask.z)
            + countbits(mask.w);
        InterlockedAdd(pc.offsets[schema], count, initial_slot);
    }

    uint write_slot = WaveReadLaneAt(initial_slot, leader_lane) + rank_in_mask(mask, wave_ix);
    pc.frag_ids[write_slot] = tid.y * pc.screen.x + tid.x;
}
