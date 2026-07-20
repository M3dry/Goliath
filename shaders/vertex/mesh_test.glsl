#version 460
#extension GL_EXT_buffer_reference2 : require

layout(buffer_reference, std430) readonly buffer GeometryBuffer {
    uint data[];
};

layout(push_constant) uniform PushConstants {
    mat4 vp;
    GeometryBuffer geo;
} pc;

// GPUGeometry layout matches Mesh.GPUGeometry extern struct
// data[0] = stride (u32)
// data[1] = position_offset (u32)
// data[2] = normal_offset (u32)
// data[3] = tangent_offset (u32)
// data[4] = texcoord0_offset (u32)
// data[5..7] = texcoord1..3_offset
// Vertex data starts at data[8]

layout(location = 0) out vec2 vUv;

void main() {
    uint stride   = pc.geo.data[0];
    uint pos_off  = pc.geo.data[1];
    uint tc0_off  = pc.geo.data[4];

    uint vtx_ix = gl_VertexIndex;
    uint base = 8 + vtx_ix * stride;

    vec3 pos = vec3(
        uintBitsToFloat(pc.geo.data[base + pos_off]),
        uintBitsToFloat(pc.geo.data[base + pos_off + 1]),
        uintBitsToFloat(pc.geo.data[base + pos_off + 2])
    );

    vUv = vec2(
        uintBitsToFloat(pc.geo.data[base + tc0_off]),
        uintBitsToFloat(pc.geo.data[base + tc0_off + 1])
    );

    gl_Position = pc.vp * vec4(pos, 1.0);
}
