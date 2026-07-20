#version 460

const vec4 COLOR_AXIS_X = vec4(0.85, 0.2, 0.2, 1.0); // Red
const vec4 COLOR_AXIS_Z = vec4(0.2, 0.45, 0.85, 1.0); // Blue
const vec4 COLOR_MAJOR  = vec4(0.40, 0.40, 0.40, 1.0);
const vec4 COLOR_MINOR  = vec4(0.30, 0.30, 0.30, 0.8);

const float MINOR_THICKNESS_SCALE = 1.0;
const float MAJOR_THICKNESS_SCALE = 2.0;
const float AXIS_THICKNESS_SCALE = MAJOR_THICKNESS_SCALE;
const float MIN_WORLD_WIDTH = 0.005; // Minimum width in normalized grid units (stops "shrinking" up close)

layout(push_constant, std430) uniform Push {
    mat4 inv_vp;
    mat4 vp;
    vec3 cam_pos;
    vec2 screen;
};

layout(location = 0) out vec4 frag_color;

vec3 reconstruct_world_ray(vec2 uv) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = inv_vp * clip;
    world /= world.w;
    return normalize(world.xyz - cam_pos);
}

bool intersects_plane(vec3 origin, vec3 direction, out float t) {
    if (abs(direction.y) < 1e-4)
        return false;
    t = -origin.y / direction.y;
    return t > 0.0;
}

float grid_line(float coord, float scale, float thickness_scale) {
    float x = coord / scale;
    float g = abs(fract(x - 0.5) - 0.5);
    float w = max(fwidth(x) * thickness_scale, MIN_WORLD_WIDTH / scale);

    return 1.0 - smoothstep(0.0, w, g);
}

float grid_axis(float coord) {
    float g = abs(coord);
    float w = max(fwidth(coord) * AXIS_THICKNESS_SCALE, MIN_WORLD_WIDTH);

    return 1.0 - smoothstep(0.0, w, g);
}

vec4 get_grid_color(vec3 world_pos) {
    float minor = max(grid_line(world_pos.x, 1.0, MINOR_THICKNESS_SCALE), grid_line(world_pos.z, 1.0, MINOR_THICKNESS_SCALE));
    float major = max(grid_line(world_pos.x, 10.0, MAJOR_THICKNESS_SCALE), grid_line(world_pos.z, 10.0, MAJOR_THICKNESS_SCALE));
    
    float xAxis = grid_axis(world_pos.z);
    float zAxis = grid_axis(world_pos.x);

    vec4 color = vec4(0.0);
    color = mix(color, COLOR_MINOR, minor);
    color = mix(color, COLOR_MAJOR, major);
    color = mix(color, COLOR_AXIS_X, xAxis);
    color = mix(color, COLOR_AXIS_Z, zAxis);

    return color;
}

float dist_fade(float t) {
    return exp(-t * 0.02);
}

float angle_fade(vec3 ray_direction) {
    return clamp(abs(ray_direction.y) * 5.0, 0.0, 1.0);
}

void main() {
    vec2 uv = gl_FragCoord.xy / screen;
    vec3 ray_direction = reconstruct_world_ray(uv);

    float t;
    if (!intersects_plane(cam_pos, ray_direction, t)) {
        discard;
    }

    vec3 p = cam_pos + ray_direction * t;

    vec4 color = get_grid_color(p);

    float fade = dist_fade(t) * angle_fade(ray_direction);
    color *= fade;

    vec4 clip_pos = vp * vec4(p, 1.0);
    gl_FragDepth = clip_pos.z/clip_pos.w;

    frag_color = color;
}
