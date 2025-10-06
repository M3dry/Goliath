#version 460

layout(location = 0) out vec2 uv;

void main() {
    if (gl_VertexIndex == 0) {
        gl_Position = vec4(0.0, 0.5, 0.0, 1.0);
        uv = vec2(0.5, 1.0);
    } else if (gl_VertexIndex == 1) {
        gl_Position = vec4(-0.5, -0.5, 0.0, 1.0);
        uv = vec2(1.0, 0.0);
    } else {
        gl_Position = vec4(0.5, -0.5, 0.0, 1.0);
        uv = vec2(0.0, 0.0);
    }
}
