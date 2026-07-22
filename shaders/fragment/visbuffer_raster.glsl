#version 460

layout(location = 0) flat in uint vis_packed;

layout(location = 0) out uint vis_buffer;

void main() {
    vis_buffer = vis_packed;
}
