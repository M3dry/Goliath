#version 460

#include "library/mesh_data.glsl"
#include "library/culled_data.glsl"

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
    DrawIDs draw_ids;
    uint mat_id;
};

layout(set = 0, binding = 0, rgba32f) uniform image2D target;
layout(set = 0, binding = 1, rgba32ui) readonly uniform uimage2D visbuffer;
layout(set = 1, binding = 0) uniform ShadingData {
    vec3 cam_pos;
    vec3 light_pos;
    vec3 light_intensity;
    mat4 view_proj_matrix;
};
layout(set = 2, binding = 0) uniform sampler2D textures[];

const float PI = 3.14159265359;

vec3 get_normal(float normal_factor, uint normal_tex, vec2 normal_uv, vec3 normal, vec3 tangent, float tangent_w) {
    return normal;
    // vec3 N = normalize(normal);
    // vec3 T = normalize(tangent - N * dot(N, tangent));
    // vec3 B = cross(N, T) * tangent_w;
    //
    // mat3 TBN = mat3(T, B, N);
    //
    // vec3 n_ts = texture(textures[normal_tex], normal_uv).xyz * 2.0 - 1.0;
    // n_ts.xy *= normal_factor;
    //
    // n_ts.z = sqrt(max(1.0 - dot(n_ts.xy, n_ts.xy), 0.0));
    //
    // return normalize(TBN * n_ts);
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

    uvec4 vis = imageLoad(visbuffer, ivec2(frag));
    if (vis.x == 0) return;

    DrawID draw_id = draw_ids.id[vis.x - 1];
    VertexData verts = draw_id.group;
    MeshData mesh_data = read_mesh_data(verts, draw_id.start_offset/4);
    uint primitive_id = vis.z;
    uint bary_ui = vis.w;
    vec3 bary = vec3(unpackHalf2x16(bary_ui), 0.0);
    bary.z = 1.0 - bary.x - bary.y;

    mat4 model_transform = draw_id.model_transform * mesh_data.transform;
    mat3 normal_matrix = transpose(inverse(mat3(model_transform)));

    PBR pbr = load_material(verts, mesh_data.offsets);

    Vertex v1 = load_vertex(verts, mesh_data.offsets, primitive_id * 3, false);
    Vertex v2 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 1, false);
    Vertex v3 = load_vertex(verts, mesh_data.offsets, primitive_id * 3 + 2, false);

    Vertex interpolated = interpolate_vertex(v1, v2, v3, bary);
    interpolated.normal = normalize(normal_matrix * interpolated.normal);
    interpolated.tangent.xyz = mat3(model_transform) * interpolated.tangent.xyz;
    float tangent_w = v1.tangent.w;
    vec3 world_pos = (model_transform * vec4(interpolated.pos, 1.0)).xyz;

    vec3 albedo = (pbr.albedo * texture(textures[pbr.albedo_map], interpolated.texcoord0)).rgb;
    float metallic = pbr.metallic_factor * texture(textures[pbr.metallic_roughness_map], interpolated.texcoord0).b;
    float roughness = pbr.roughness_factor * texture(textures[pbr.metallic_roughness_map], interpolated.texcoord0).g;
    vec3 normal = get_normal(pbr.normal_factor, pbr.normal_map, interpolated.texcoord0, interpolated.normal, interpolated.tangent.xyz, tangent_w);
    float occlusion = pbr.occlusion_factor * texture(textures[pbr.occlusion_map], interpolated.texcoord0).r;
    vec3 emissive = pbr.emissive_factor * texture(textures[pbr.emissive_map], interpolated.texcoord0).rgb;

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
