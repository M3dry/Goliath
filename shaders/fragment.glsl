#version 460

layout(location = 0) out vec4 frag_color;

layout(push_constant, std430) uniform Push {
    vec4 color;
};

void main() {
    frag_color = color;
}
