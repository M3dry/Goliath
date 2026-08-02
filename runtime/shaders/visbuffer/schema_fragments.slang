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

[shader("compute")]
[numthreads(16, 16, 1)]
void compute(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= pc.screen.x || tid.y >= pc.screen.y) return;

    VisFragment vis = read_vis_fragment(visbuffer[tid.xy]);
    if (vis.draw_id == 0) return;

    uint schema = pc.renderables.data[vis.draw_id - 1].material_schema;
    if (schema > pc.max_schema) return;

    uint slot;
    InterlockedAdd(pc.offsets[schema], 1, slot);
    pc.frag_ids[slot] = tid.x * pc.screen.x + tid.y;
}
