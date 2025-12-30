#ifndef _SHADING_DATA_
#define _SHADING_DATA_

#include "library/vertex_data.glsl"

layout(set = 2, binding = 0) uniform sampler2D textures[];

#define get_texture(gid) textures[gid & 0x00FFFFFFu]

struct BarycentricDeriv {
    vec3 lambda;
    vec3 ddx;
    vec3 ddy;
};

BarycentricDeriv CalcFullBary(vec4 pt0, vec4 pt1, vec4 pt2, vec2 pixelNdc, vec2 winSize) {
    BarycentricDeriv ret;

    vec3 invW = 1.0 / vec3(pt0.w, pt1.w, pt2.w);

    vec2 ndc0 = pt0.xy * invW.x;
    vec2 ndc1 = pt1.xy * invW.y;
    vec2 ndc2 = pt2.xy * invW.z;

    float det = determinant(mat2(ndc2 - ndc1, ndc0 - ndc1));
    float invDet = 1.0 / (abs(det) > 1e-12 ? det : 1e-12);

    ret.ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    
    float ddxSum = dot(ret.ddx, vec3(1.0));
    float ddySum = dot(ret.ddy, vec3(1.0));

    vec2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = 1.0 / interpInvW;

    ret.lambda.x = interpW * (invW.x + deltaVec.x * ret.ddx.x + deltaVec.y * ret.ddy.x);
    ret.lambda.y = interpW * (0.0    + deltaVec.x * ret.ddx.y + deltaVec.y * ret.ddy.y);
    ret.lambda.z = interpW * (0.0    + deltaVec.x * ret.ddx.z + deltaVec.y * ret.ddy.z);

    ret.ddx *= (2.0 / winSize.x);
    ret.ddy *= (2.0 / winSize.y);
    ddxSum  *= (2.0 / winSize.x);
    ddySum  *= (2.0 / winSize.y);

    float interpW_ddx = 1.0 / (interpInvW + ddxSum);
    float interpW_ddy = 1.0 / (interpInvW + ddySum);

    ret.ddx = interpW_ddx * (ret.lambda * interpInvW + ret.ddx) - ret.lambda;
    ret.ddy = interpW_ddy * (ret.lambda * interpInvW + ret.ddy) - ret.lambda;

    ret.lambda = max(ret.lambda, vec3(0.0));
    return ret;
}

struct Interpolated {
    float value;
    float ddx;
    float ddy;
};

struct Interpolated2 {
    vec2 value;
    vec2 ddx;
    vec2 ddy;
};

struct Interpolated3 {
    vec3 value;
    vec3 ddx;
    vec3 ddy;
};

Interpolated interpolate(BarycentricDeriv deriv, float v1, float v2, float v3) {
    vec3 mergedV = vec3(v1, v2, v3);
    Interpolated ret;

    ret.value = dot(mergedV, deriv.lambda);
    ret.ddx = dot(mergedV, deriv.ddx);
    ret.ddy = dot(mergedV, deriv.ddy);

    return ret;
}

Interpolated2 interpolate2(BarycentricDeriv deriv, vec2 v1, vec2 v2, vec2 v3) {
    Interpolated interp1 = interpolate(deriv, v1.x, v2.x, v3.x);
    Interpolated interp2 = interpolate(deriv, v1.y, v2.y, v3.y);

    return Interpolated2(vec2(interp1.value, interp2.value), vec2(interp1.ddx, interp2.ddx), vec2(interp1.ddy, interp2.ddy));
}

Interpolated3 interpolate3(BarycentricDeriv deriv, vec3 v1, vec3 v2, vec3 v3) {
    Interpolated interp1 = interpolate(deriv, v1.x, v2.x, v3.x);
    Interpolated interp2 = interpolate(deriv, v1.y, v2.y, v3.y);
    Interpolated interp3 = interpolate(deriv, v1.z, v2.z, v3.z);

    return Interpolated3(vec3(interp1.value, interp2.value, interp3.value), vec3(interp1.ddx, interp2.ddx, interp3.ddx), vec3(interp1.ddy, interp2.ddy, interp3.ddy));
}

struct InterpolatedVertex {
    Interpolated3 pos;
    Interpolated3 normal;
    Interpolated3 tangent;
    float tangent_w;
    Interpolated2 texcoord0;
    Interpolated2 texcoord1;
    Interpolated2 texcoord2;
    Interpolated2 texcoord3;
};

InterpolatedVertex interpolate_vertex(uvec2 screen, uvec2 fragment, mat4 m, mat4 vp, Vertex v1, Vertex v2, Vertex v3) {
    vec2 fragment_ndc = vec2(
            ((fragment.x + 0.5) / screen.x) * 2.0 - 1.0,
            ((fragment.y + 0.5) / screen.y) * 2.0 - 1.0
        );

    mat4 mvp = vp * m;
    vec4 clip0 = mvp * vec4(v1.pos, 1.0);
    vec4 clip1 = mvp * vec4(v2.pos, 1.0);
    vec4 clip2 = mvp * vec4(v3.pos, 1.0);

    BarycentricDeriv deriv = CalcFullBary(clip0, clip1, clip2, fragment_ndc, vec2(screen));

    InterpolatedVertex v;

    vec3 p0 = (m * vec4(v1.pos, 1.0)).xyz;
    vec3 p1 = (m * vec4(v2.pos, 1.0)).xyz;
    vec3 p2 = (m * vec4(v3.pos, 1.0)).xyz;
    v.pos = interpolate3(deriv, p0, p1, p2);

    v.normal = interpolate3(deriv, v1.normal, v2.normal, v3.normal);
    v.tangent = interpolate3(deriv, v1.tangent.xyz, v2.tangent.xyz, v3.tangent.xyz);
    v.tangent_w = v1.tangent.w;
    v.texcoord0 = interpolate2(deriv, v1.texcoord0, v2.texcoord0, v3.texcoord0);
    v.texcoord1 = interpolate2(deriv, v1.texcoord1, v2.texcoord1, v3.texcoord1);
    v.texcoord2 = interpolate2(deriv, v1.texcoord2, v2.texcoord2, v3.texcoord2);
    v.texcoord3 = interpolate2(deriv, v1.texcoord3, v2.texcoord3, v3.texcoord3);

    return v;
}

#endif
