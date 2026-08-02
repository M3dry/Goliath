import data;

public struct PBRInstance {
    public uint albedo_map;
    public uint metallic_roughness_map;
    public uint normal_map;
    public uint occlusion_map;
    public uint emissive_map;

    public uint albedo_texcoord;
    public uint metallic_roughness_texcoord;
    public uint normal_texcoord;
    public uint occlusion_texcoord;
    public uint emissive_texcoord;

    public float4 albedo;
    public float metallic_factor;
    public float roughness_factor;
    public float normal_factor;
    public float occlusion_factor;
    public float3 emissive_factor;
};

public struct Light {
    public float3 position;
    public float3 intensity;
};

public struct LightBuffer {
    public Light light[];
};

struct PBRPC {
    uint2 screen;
    DispatchEntry* dispatch;
    uint* frag_ids;
    RenderableBuffer* renderables;
    PBRInstance* instances;
};

[[vk::push_constant]] ConstantBuffer<PBRPC> pc;

[[vk::binding(0, 0)]] RWTexture2D<uint> visbuffer;
[[vk::binding(1, 0)]] RWTexture2D<float4> target;

[[vk::binding(0, 1)]] Texture2D textures[];
[[vk::binding(0, 1)]] SamplerState samplers[];

struct ShadingData {
    float3 cam_pos;
    float4x4 view_proj_matrix;
    LightBuffer* lights;
    uint light_count;
};

[[vk::binding(0, 2)]] ConstantBuffer<ShadingData> shading_data;

static const float PI = 3.14159265359;

Texture2D get_texture(uint gid) {
    return textures[NonUniformResourceIndex(gid & 0x00FFFFFFu)];
}

SamplerState get_sampler(uint gid) {
    return samplers[NonUniformResourceIndex(gid & 0x00FFFFFFu)];
}

float3x3 reconstruct_TBN(float3 N, Interpolated3 pos, Interpolated2 uv) {
    float3 dp1 = pos.ddx;
    float3 dp2 = pos.ddy;
    float2 duv1 = uv.ddx;
    float2 duv2 = uv.ddy;

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);

    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = rsqrt(max(dot(T, T), dot(B, B)));

    return float3x3(T * invmax, B * invmax, N);
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 aces_tonemap(float3 color) {
    float3x3 m1 = float3x3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    );
    float3x3 m2 = float3x3(
        1.60475, -0.10208, -0.00327,
        -0.53108,  1.10813, -0.07276,
        -0.07367, -0.00605,  1.07602
    );

    float3 v = mul(m1, color);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;

    return pow(clamp(mul(m2, (a / b)), 0.0, 1.0), float3(1.0 / 2.2));
}

[shader("compute")]
[numthreads(256, 1, 1)]
void compute(uint3 tid : SV_DispatchThreadID) {
    uint gid = tid.x;
    if (gid >= pc.dispatch[0].count) return;

    uint frag_id = pc.frag_ids[pc.dispatch[0].offset + gid];
    uint2 frag = uint2(frag_id / pc.screen.x, frag_id % pc.screen.x);
    if (frag.x >= pc.screen.x || frag.y >= pc.screen.y) return;

    VisFragment vis = read_vis_fragment(visbuffer[frag]);
    if (vis.draw_id == 0) return;

    Renderable rend = pc.renderables.data[vis.draw_id - 1];
    uint primitive_id = vis.primitive_id;

    PBRInstance inst = pc.instances[rend.material_instance];

    Vertex v1 = load_vertex(rend.geometry, primitive_id * 3 + 0);
    Vertex v2 = load_vertex(rend.geometry, primitive_id * 3 + 1);
    Vertex v3 = load_vertex(rend.geometry, primitive_id * 3 + 2);

    InterpolatedVertex interp = interpolate_vertex(pc.screen, frag, rend.transform, shading_data.view_proj_matrix, v1, v2, v3);
    float3 world_normal = normalize(interp.normal.value);

    float3 world_pos = interp.pos.value;

    float2 uv0 = interp.texcoord0.value;
    float2 uv0_ddx = interp.texcoord0.ddx;
    float2 uv0_ddy = interp.texcoord0.ddy;

    float3 albedo = (inst.albedo * get_texture(inst.albedo_map).SampleGrad(get_sampler(inst.albedo_map), uv0, uv0_ddx, uv0_ddy)).rgb;
    float metallic = inst.metallic_factor * get_texture(inst.metallic_roughness_map).SampleGrad(get_sampler(inst.metallic_roughness_map), uv0, uv0_ddx, uv0_ddy).b;
    float roughness = inst.roughness_factor * get_texture(inst.metallic_roughness_map).SampleGrad(get_sampler(inst.metallic_roughness_map), uv0, uv0_ddx, uv0_ddy).g;
    float3 normal_map_value = get_texture(inst.normal_map).SampleGrad(get_sampler(inst.normal_map), uv0, uv0_ddx, uv0_ddy).rgb;
    normal_map_value = normal_map_value * 2.0 - 1.0;
    normal_map_value.xy *= inst.normal_factor;
    float occlusion = inst.occlusion_factor * get_texture(inst.occlusion_map).SampleGrad(get_sampler(inst.occlusion_map), uv0, uv0_ddx, uv0_ddy).r;
    float3 emissive = inst.emissive_factor * get_texture(inst.emissive_map).SampleGrad(get_sampler(inst.emissive_map), uv0, uv0_ddx, uv0_ddy).rgb;

    float3x3 TBN_normal = reconstruct_TBN(world_normal, interp.pos, interp.texcoord0);
    float3 normal = normalize(mul(TBN_normal, normal_map_value));

    float3 view = normalize(shading_data.cam_pos - world_pos);
    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 Lo = float3(0.0);
    // for (uint i = 0; i < shading_data.light_count; i++) {
        // Light light = shading_data.lights.light[i];
        Light light = Light(float3(5, 5, 5), float3(1000));

        float3 L = normalize(light.position - world_pos);
        float3 H = normalize(view + L);
        float distance = length(light.position - world_pos);
        float attenuation = 1.0 / max(distance * distance, 0.001);
        float3 radiance = light.intensity * attenuation;

        float NDF = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, view, L, roughness);
        float3 F = fresnelSchlick(max(dot(H, view), 0.0), F0);

        float3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(normal, view), 0.0) * max(dot(normal, L), 0.0) + 0.0001;
        float3 specular = numerator / denominator;

        float3 kS = F;
        float3 kD = float3(1.0) - kS;
        kD *= 1.0 - metallic;
        float NdotL = max(dot(normal, L), 0.0);

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    // }

    float3 ambient = float3(0.03) * albedo * occlusion;
    float3 color = ambient + Lo + emissive;
    color = aces_tonemap(color);

    target[frag] = float4(color, 1.0);
}
