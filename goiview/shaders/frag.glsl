#version 460

layout(push_constant, std430) uniform Push {
    uvec2 screen_dims;
    uvec2 image_dims;
};

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 frag_color;

void main() {
    vec2 screen = vec2(screen_dims);
    vec2 image  = vec2(image_dims);

    vec2 pixel = gl_FragCoord.xy;

    // Compute uniform max-fit scale
    float scale = min(screen.x / image.x, screen.y / image.y);
    if (scale > 1.0) {
        scale = floor(scale);
    }

    vec2 draw_size = image * scale;
    vec2 offset    = (screen - draw_size) * 0.5;

    // Outside fitted image → black
    if (pixel.x < offset.x || pixel.y < offset.y ||
        pixel.x >= offset.x + draw_size.x ||
        pixel.y >= offset.y + draw_size.y)
    {
        frag_color = vec4(0.0);
        return;
    }

    // Map pixel → image UV
    vec2 uv = (pixel - offset) / draw_size;
    uv = clamp(uv, vec2(0.0), vec2(1.0));

    frag_color = texture(tex, uv);
}
