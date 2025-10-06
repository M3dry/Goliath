#version 460

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 frag_color;

layout(set = 3, binding = 0) uniform sampler2D textures[];

layout(push_constant, std430) uniform Push {
    vec4 color;
};

void main() {
    frag_color = texture(textures[0], uv);
}
