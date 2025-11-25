#version 460

#include "library/mesh_data.glsl"

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_nonuniform_qualifier : require

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(buffer_reference, std430) readonly buffer DispatchCommand {
    uint val[5];
};

layout(buffer_reference, std430) readonly buffer FragIDs {
    uint id[];
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    DispatchCommand dispatch;
    FragIDs frag_ids;
    uint mat_id;
};

layout (set = 1, binding = 0) uniform sampler2D textures[];

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, rgba32ui) readonly uniform uimage2D visbuffer;

struct PBR {
    uint albedo_map;
    uint metallic_roughness_map;
    uint normal_map;
    uint occlusion_map;
    uint emissive_map;

    uint albedo_texcoord;
    uint metallic_roughness_texcoord;
    uint normal_texcoord;
    uint occlusion_texcoord;
    uint emissive_texcoord;

    vec4 albedo;
    float metallic_factor;
    float roughness_factor;
    float normal_factor;
    float occlusion_factor;
    vec3 emissive_factor;
};

PBR load_material(const VertexData verts, const Offsets offs) {
    PBR ret;

    uint off = offs.relative_start/4 + offs.material_offset/4;
    ret.albedo_map = verts.data[off];
    ret.metallic_roughness_map = verts.data[off + 1];
    ret.normal_map = verts.data[off + 2];
    ret.occlusion_map = verts.data[off + 3];
    ret.emissive_map = verts.data[off + 4];

    ret.albedo_texcoord = verts.data[off + 5];
    ret.metallic_roughness_texcoord = verts.data[off + 6];
    ret.normal_texcoord = verts.data[off + 7];
    ret.occlusion_texcoord = verts.data[off + 8];
    ret.emissive_texcoord = verts.data[off + 9];

    ret.albedo.x = uintBitsToFloat(verts.data[off + 10]);
    ret.albedo.y = uintBitsToFloat(verts.data[off + 11]);
    ret.albedo.z = uintBitsToFloat(verts.data[off + 12]);
    ret.albedo.w = uintBitsToFloat(verts.data[off + 13]);

    ret.metallic_factor = uintBitsToFloat(verts.data[off + 14]);
    ret.roughness_factor = uintBitsToFloat(verts.data[off + 15]);
    ret.normal_factor = uintBitsToFloat(verts.data[off + 16]);
    ret.occlusion_factor = uintBitsToFloat(verts.data[off + 17]);

    ret.emissive_factor.x = uintBitsToFloat(verts.data[off + 18]);
    ret.emissive_factor.y = uintBitsToFloat(verts.data[off + 19]);
    ret.emissive_factor.z = uintBitsToFloat(verts.data[off + 20]);

    return ret;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= dispatch.val[4]) return;

    uint frag_id = frag_ids.id[dispatch.val[3] + gid];
    uvec2 frag = uvec2(frag_id / screen.x, frag_id % screen.x);
    if (frag.x >= screen.x || frag.y >= screen.y)  return;

    uvec4 vis = imageLoad(visbuffer, ivec2(frag));
    if (vis.x == 0 && vis.y == 0) return;

    VertexData verts = VertexData(vis.xy);
    MeshData mesh_data = read_mesh_data(verts, 0);
    uint primitive_id = vis.z;
    uint bary_ui = vis.w;
    vec3 bary = vec3(unpackHalf2x16(bary_ui), 0.0);
    bary.z = 1.0 - bary.x - bary.y;

    PBR pbr = load_material(verts, mesh_data.offsets);

    Vertex v1 = load_vertex(verts, mesh_data.offsets, primitive_id * 3, true);
    Vertex v2 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 1, true);
    Vertex v3 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 2, true);
    Vertex interpolated = interpolate_vertex(v1, v2, v3, bary);

    imageStore(target, ivec2(frag), texture(textures[pbr.albedo_map], interpolated.texcoord0));
}
