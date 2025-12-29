#version 460

layout(push_constant, std430) uniform Push {
    uvec2 screen_dims;
    uvec2 image_dims;
};

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) out vec4 frag_color;

void main() {
    uvec2 pixel = uvec2(gl_FragCoord.xy);

    if (pixel.x >= image_dims.x|| pixel.y >= image_dims.y) {
        frag_color = vec4(0.0);
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(image_dims);
    frag_color = texture(image, uv);
}
