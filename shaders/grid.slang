// Grid shader in Slang (Vulkan)

static const float4 COLOR_AXIS_X = float4(0.85f, 0.2f, 0.2f, 1.0f);
static const float4 COLOR_AXIS_Z = float4(0.2f, 0.45f, 0.85f, 1.0f);
static const float4 COLOR_MAJOR  = float4(0.40f, 0.40f, 0.40f, 1.0f);
static const float4 COLOR_MINOR  = float4(0.30f, 0.30f, 0.30f, 0.8f);

static const float MINOR_THICKNESS_SCALE = 1.0f;
static const float MAJOR_THICKNESS_SCALE = 2.0f;
static const float AXIS_THICKNESS_SCALE  = MAJOR_THICKNESS_SCALE;
static const float MIN_WORLD_WIDTH       = 0.005f;  // Minimum width in normalized grid units

struct PushConstants {
    float4x4 inv_vp;
    float4x4 vp;
    float3   cam_pos;
    float2   screen;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> push;

float3 reconstruct_world_ray(float2 uv) {
    float4 clip = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    float4 world = mul(push.inv_vp, clip);
    world /= world.w;

    return normalize(world.xyz - push.cam_pos);
}

bool intersects_plane(float3 origin, float3 direction, out float t) {
    if (abs(direction.y) < 1e-4f) return false;

    t = -origin.y / direction.y;
    return t > 0.0f;
}

float grid_line(float coord, float scale, float thickness_scale) {
    float x = coord / scale;
    float g = abs(frac(x - 0.5f) - 0.5f);
    float w = max(fwidth(x) * thickness_scale, MIN_WORLD_WIDTH / scale);

    return 1.0f - smoothstep(0.0f, w, g);
}

float grid_axis(float coord) {
    float g = abs(coord);
    float w = max(fwidth(coord) * AXIS_THICKNESS_SCALE, MIN_WORLD_WIDTH);

    return 1.0f - smoothstep(0.0f, w, g);
}

float4 get_grid_color(float3 world_pos) {
    float minor = max(grid_line(world_pos.x, 1.0f, MINOR_THICKNESS_SCALE),
                      grid_line(world_pos.z, 1.0f, MINOR_THICKNESS_SCALE));
    float major = max(grid_line(world_pos.x, 10.0f, MAJOR_THICKNESS_SCALE),
                      grid_line(world_pos.z, 10.0f, MAJOR_THICKNESS_SCALE));

    float xAxis = grid_axis(world_pos.z);
    float zAxis = grid_axis(world_pos.x);

    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    color = lerp(color, COLOR_MINOR, minor);
    color = lerp(color, COLOR_MAJOR, major);
    color = lerp(color, COLOR_AXIS_X, xAxis);
    color = lerp(color, COLOR_AXIS_Z, zAxis);

    return color;
}

float dist_fade(float t) {
    return exp(-t * 0.02f);
}

float angle_fade(float3 ray_direction) {
    return clamp(abs(ray_direction.y) * 5.0f, 0.0f, 1.0f);
}

struct PSOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
}

[shader("fragment")]
PSOutput fragment(float4 fragCoord : SV_Position) {
    PSOutput output;

    float2 uv = fragCoord.xy / push.screen;
    float3 ray_dir = reconstruct_world_ray(uv);

    float t;
    if (!intersects_plane(push.cam_pos, ray_dir, t)) discard;

    float3 p = push.cam_pos + ray_dir * t;

    float4 color = get_grid_color(p);

    float fade = dist_fade(t) * angle_fade(ray_dir);
    output.color = color * fade;

    float4 clip_pos = mul(push.vp, float4(p, 1.0f));
    output.depth = clip_pos.z / clip_pos.w;

    return output;
}
