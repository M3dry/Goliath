import data;

struct PC {
    float4x4 vp;
    WorldInstance* world_instances;
    MeshDesc* mesh_descs;
    LODEntry* lod_entries;
    RenderableBuffer* renderables;
    DrawCmdBuffer* draw_cmds;
    uint instance_count;
    uint max_draw_count;
    float screen_width;
    float screen_height;
    float fov_y;
    float3 camera_pos;
};

[[vk::push_constant]] ConstantBuffer<PC> pc;

bool frustum_test(float4x4 vp, float4x4 model, float3 bmin, float3 bmax) {
    float3 corners[8] = {
        float3(bmin.x, bmin.y, bmin.z),
        float3(bmax.x, bmin.y, bmin.z),
        float3(bmin.x, bmax.y, bmin.z),
        float3(bmax.x, bmax.y, bmin.z),
        float3(bmin.x, bmin.y, bmax.z),
        float3(bmax.x, bmin.y, bmax.z),
        float3(bmin.x, bmax.y, bmax.z),
        float3(bmax.x, bmax.y, bmax.z)
    };

    for (int plane = 0; plane < 4; plane++) {
        int out_count = 0;
        for (int c = 0; c < 8; c++) {
            float4 world = mul(model, float4(corners[c], 1.0));
            float4 clip = mul(vp, world);
            if (clip[plane] < -clip.w) out_count++;
        }
        if (out_count == 8) return false;
    }
    return true;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void compute(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= pc.instance_count) return;

    WorldInstance inst = pc.world_instances[tid.x];
    MeshDesc md = pc.mesh_descs[inst.mesh_desc_ix];

    if (!frustum_test(pc.vp, inst.transform, md.min, md.max)) return;

    float3 center = mul(inst.transform, float4(0.5 * (md.min + md.max), 1.0)).xyz;
    float dist = length(center - pc.camera_pos);
    float ppf = 2.0 * tan(pc.fov_y * 0.5) / pc.screen_height;

    uint selected_lod = md.lod_count;
    for (uint i = 0; i < md.lod_count; i++) {
        LODEntry le = pc.lod_entries[md.lod_offset + i];
        if (uint64_t(le.geometry) == 0) continue;

        float pixel_size = le.error_metric / (dist * ppf + 0.001);
        if (pixel_size > 1.0) {
            selected_lod = i;
            break;
        }
    }

    if (selected_lod == md.lod_count) return;

    LODEntry lod = pc.lod_entries[md.lod_offset + selected_lod];

    uint slot;
    InterlockedAdd(pc.renderables.count, 1, slot);
    if (slot >= pc.max_draw_count) {
        uint min_val = pc.max_draw_count;
        InterlockedMin(pc.renderables.count, min_val);
        return;
    }

    uint dslot;
    InterlockedAdd(pc.draw_cmds.count, 1, dslot);
    if (dslot >= pc.max_draw_count) {
        uint min_val = pc.max_draw_count;
        InterlockedMin(pc.draw_cmds.count, min_val);
        return;
    }

    pc.renderables.data[slot].transform = inst.transform;
    pc.renderables.data[slot].geometry = lod.geometry;
    pc.renderables.data[slot].material_schema = lod.material_schema;
    pc.renderables.data[slot].material_instance = lod.material_instance;

    pc.draw_cmds.cmd[dslot].draw_count = lod.draw_count;
    pc.draw_cmds.cmd[dslot].instance_count = 1;
    pc.draw_cmds.cmd[dslot].first_vertex = 0;
    pc.draw_cmds.cmd[dslot].first_instance = 0;
    pc.draw_cmds.cmd[dslot].renderable_ix = slot;
}
