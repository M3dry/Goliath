#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VertexData {
    uint data[];
};

layout(push_constant, std430) uniform Push {
    VertexData verts;
    mat4 m;
    mat4 vp;

    // start, stride, material offset, indicies offset
    uvec4 off1;

    // position offset, normal offset, tangent offset, texcoordd0 offset
    uvec4 off2;

    // texcoord 1..2 offsets
    uvec3 off3;
};

layout(location = 0) out vec3 f_position;
layout(location = 1) out vec3 f_normal;
layout(location = 2) out vec4 f_tangent;
layout(location = 3) out vec2 f_texcoord0;
layout(location = 4) out vec2 f_texcoord1;
layout(location = 5) out vec2 f_texcoord2;
layout(location = 6) out vec2 f_texcoord3;

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec4 tangent;
    vec2 texcoord0;
    vec2 texcoord1;
    vec2 texcoord2;
    vec2 texcoord3;
};
Vertex load_vertex() {
    Vertex vert;

    uint ix = gl_VertexIndex;

    uint start = off1.x/4;
    if (off1.w != uint(-1)) {
        ix = verts.data[start + off1.w/4 + ix];
    }

    uint stride = off1.y/4;

    if (off2.x != uint(-1)) {
        vert.pos.x = uintBitsToFloat(verts.data[start + off2.x/4 + ix*stride]);
        vert.pos.y = uintBitsToFloat(verts.data[start + off2.x/4 + ix*stride + 1]);
        vert.pos.z = uintBitsToFloat(verts.data[start + off2.x/4 + ix*stride + 2]);
    }

    if (off2.y != uint(-1)) {
        vert.normal.x = uintBitsToFloat(verts.data[start + off2.y/4 + ix*stride]);
        vert.normal.y = uintBitsToFloat(verts.data[start + off2.y/4 + ix*stride + 1]);
        vert.normal.z = uintBitsToFloat(verts.data[start + off2.y/4 + ix*stride + 2]);
    }

    if (off2.z != uint(-1)) {
        vert.tangent.x = uintBitsToFloat(verts.data[start + off2.z/4 + ix*stride]);
        vert.tangent.y = uintBitsToFloat(verts.data[start + off2.z/4 + ix*stride + 1]);
        vert.tangent.z = uintBitsToFloat(verts.data[start + off2.z/4 + ix*stride + 2]);
        vert.tangent.w = uintBitsToFloat(verts.data[start + off2.z/4 + ix*stride + 3]);
    }

    if (off2.w != uint(-1)) {
        vert.texcoord0.x = uintBitsToFloat(verts.data[start + off2.w/4 + ix*stride]);
        vert.texcoord0.y = uintBitsToFloat(verts.data[start + off2.w/4 + ix*stride + 1]);
    }

    if (off3.x != uint(-1)) {
        vert.texcoord1.x = uintBitsToFloat(verts.data[start + off3.x/4 + ix*stride]);
        vert.texcoord1.y = uintBitsToFloat(verts.data[start + off3.x/4 + ix*stride + 1]);
    }

    if (off3.y != uint(-1)) {
        vert.texcoord2.x = uintBitsToFloat(verts.data[start + off3.y/4 + ix*stride]);
        vert.texcoord2.y = uintBitsToFloat(verts.data[start + off3.y/4 + ix*stride + 1]);
    }

    if (off3.z != uint(-1)) {
        vert.texcoord3.x = uintBitsToFloat(verts.data[start + off3.z/4 + ix*stride]);
        vert.texcoord3.y = uintBitsToFloat(verts.data[start + off3.z/4 + ix*stride + 1]);
    }

    return vert;
}

void main() {
    Vertex vert = load_vertex();

    gl_Position = vp * m * vec4(vert.pos, 1.0);
    f_normal = vert.normal;
    f_tangent = vert.tangent;
    f_texcoord0 = vert.texcoord0;
    f_texcoord1 = vert.texcoord1;
    f_texcoord2 = vert.texcoord2;
    f_texcoord3 = vert.texcoord3;

    // if (gl_VertexIndex == 0) {
    //     gl_Position = vec4(5.0, 0.0, 0.0, 1.0);
    // } else if (gl_VertexIndex == 1) {
    //     gl_Position = vec4(-5.0, 0.0, 0.0, 1.0);
    // } else if (gl_VertexIndex == 2) {
    //     gl_Position = vec4(0.0, 0.0, 5.0, 1.0);
    // } else {
    //     gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    // }
    //
    // gl_Position = vp * m * gl_Position;
}
