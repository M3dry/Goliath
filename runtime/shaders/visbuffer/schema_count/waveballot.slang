import data;

struct PC {
    uint2 screen;
    RenderableBuffer* renderables;
    uint* counters;
};

[[vk::push_constant]] ConstantBuffer<PC> pc;

[[vk::binding(0, 0)]] RWTexture2D<uint> visbuffer;

uint first_set_lane(uint4 mask) {
    if (mask.x != 0) return firstbitlow(mask.x);
    if (mask.y != 0) return 32 + firstbitlow(mask.y);
    if (mask.z != 0) return 64 + firstbitlow(mask.z);
    return 96 + firstbitlow(mask.w);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void compute(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= pc.screen.x || tid.y >= pc.screen.y) return;

    VisFragment vis = read_vis_fragment(visbuffer[tid.xy]);
    if (vis.draw_id == 0) return;

    Renderable rend = pc.renderables.data[vis.draw_id - 1];
    uint schema = rend.material_schema;

    // Process one distinct schema among this subgroup's live lanes per
    // iteration. Every matching lane is removed from pending together.
    uint4 pending = WaveActiveBallot(true);
    while (any(pending != uint4(0, 0, 0, 0))) {
        uint leader_lane = first_set_lane(pending);
        uint leader_schema = WaveReadLaneAt(schema, leader_lane);
        bool matches = schema == leader_schema;
        uint4 match_mask = WaveActiveBallot(matches);

        if (WaveGetLaneIndex() == leader_lane) {
            InterlockedAdd(pc.counters[leader_schema], countbits(match_mask.x) + countbits(match_mask.y) + countbits(match_mask.z) + countbits(match_mask.w));
        }

        pending &= ~match_mask;
    }
}
