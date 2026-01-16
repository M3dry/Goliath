#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "library/visbuffer_data.glsl"
#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"
#include "library/shading_data.glsl"

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(buffer_reference, std430) readonly buffer DispatchCommand {
    uint val[5];
};

layout(buffer_reference, std430) readonly buffer FragIDs {
    uint id[];
};

layout(buffer_reference, std430) readonly buffer Materials {
    uint offset_count;
    uint[] data;
};

layout(push_constant, std430) uniform Push {
    uvec2 screen;
    DispatchCommand dispatch;
    FragIDs frag_ids;
    DrawIDs draw_ids;
    uint mat_id;
    Materials mats;
};

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, r32ui) readonly uniform uimage2D visbuffer;
layout(set = 1, binding = 0) uniform ShadingData {
    vec3 cam_pos;
    vec3 light_pos;
    vec3 light_intensity;
    mat4 view_proj_matrix;
};

const float PI = 3.14159265359;

mat3 reconstruct_TBN(vec3 N, Interpolated3 pos, Interpolated2 uv) {
    
    // 1. Get the gradients
    vec3 dp1 = pos.ddx;
    vec3 dp2 = pos.ddy;
    vec2 duv1 = uv.ddx;
    vec2 duv2 = uv.ddy;

    // 2. Solve the linear system
    // The determinant of the UV matrix
    // (This is effectively the signed area of the UV triangle in screen space)
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    
    // We construct T and B simultaneously to handle non-orthogonality
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // 3. Construct the inverse determinant
    // If the determinant is zero (degenerate UVs), this handles it gracefully-ish
    float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
    
    // 4. Normalize and Gram-Schmidt Orthogonalize
    // This ensures T is perpendicular to N, even if the mesh is warped
    return mat3(T * invmax, B * invmax, N);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom/denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 aces_tonemap(vec3 color) {
    mat3 m1 = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    );
    mat3 m2 = mat3(
        1.60475, -0.10208, -0.00327,
        -0.53108,  1.10813, -0.07276,
        -0.07367, -0.00605,  1.07602
    );

    vec3 v = m1 * color;    
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;

    return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));	
}


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

    uint off = offs.start/4 + offs.material_offset/4;
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

    VisFragment vis = read_vis_fragment(imageLoad(visbuffer, ivec2(frag)).x);
    if (vis.draw_id == 0) return;

    DrawID draw_id = read_draw_id(draw_ids, vis.draw_id - 1);
    VertexData verts = draw_id.group;
    MeshData mesh_data = read_mesh_data(verts, draw_id.start_offset/4);
    uint primitive_id = vis.primitive_id;

    mat4 model_transform = read_draw_id_transform(draw_id) * mesh_data.transform;
    mat3 normal_transform = mat3(model_transform); // assumes model_transform is uniform

    PBR pbr = load_material(verts, mesh_data.offsets);

    Vertex v1 = load_vertex(verts, mesh_data.offsets, primitive_id * 3, false);
    Vertex v2 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 1, false);
    Vertex v3 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 2, false);

    InterpolatedVertex interpolated = interpolate_vertex(screen, frag, model_transform, view_proj_matrix, v1, v2, v3);
    interpolated.normal.value = normalize(normal_transform * interpolated.normal.value);

    vec3 world_pos = (view_proj_matrix * vec4(interpolated.pos.value, 1.0)).xyz;

    vec3 albedo = (pbr.albedo * textureGrad(get_texture(pbr.albedo_map), interpolated.texcoord0.value, interpolated.texcoord0.ddx, interpolated.texcoord0.ddy)).rgb;
    float metallic = pbr.metallic_factor * textureGrad(get_texture(pbr.metallic_roughness_map), interpolated.texcoord0.value, interpolated.texcoord0.ddx, interpolated.texcoord0.ddy).b;
    float roughness = pbr.roughness_factor * textureGrad(get_texture(pbr.metallic_roughness_map), interpolated.texcoord0.value, interpolated.texcoord0.ddx, interpolated.texcoord0.ddy).g;
    vec3 normal_map_value = pbr.normal_factor * textureGrad(get_texture(pbr.normal_map), interpolated.texcoord0.value, interpolated.texcoord0.ddx, interpolated.texcoord0.ddy).rgb;
    normal_map_value = normal_map_value * 2.0 - 1.0;
    float occlusion = pbr.occlusion_factor * textureGrad(get_texture(pbr.occlusion_map), interpolated.texcoord0.value, interpolated.texcoord0.ddx, interpolated.texcoord0.ddy).r;
    vec3 emissive = pbr.emissive_factor * textureGrad(get_texture(pbr.emissive_map), interpolated.texcoord0.value, interpolated.texcoord0.ddx, interpolated.texcoord0.ddy).rgb;

    mat3 TBN = reconstruct_TBN(interpolated.normal.value, interpolated.pos, interpolated.texcoord0);
    vec3 normal = normalize(TBN * normal_map_value);

    vec3 view = normalize(cam_pos - world_pos);
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    // for (uint i = 0; i < 1; i++) {
        vec3 L = normalize(light_pos - world_pos);
        vec3 H = normalize(view + L);
        float distance = length(light_pos - world_pos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = light_intensity * attenuation;

        float NDF = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, view, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, view), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(normal, view), 0.0) * max(dot(normal, L), 0.0) + 0.0001;
        vec3 specular = numerator/denominator;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;
        float NdotL = max(dot(normal, L), 0.0);

        Lo += (kD * albedo/PI + specular) * radiance * NdotL;
    // }

    vec3 ambient = vec3(0.03) * albedo * occlusion;
    vec3 color = ambient + Lo + emissive;
    color = aces_tonemap(color);

    imageStore(target, ivec2(frag), vec4(color, 1.0));
}
