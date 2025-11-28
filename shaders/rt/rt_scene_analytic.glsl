// rt_scene_analytic.glsl
#ifndef RT_SCENE_ANALYTIC_GLSL
#define RT_SCENE_ANALYTIC_GLSL

// -------- Analytic scene constants (shared with glass shading) --------
const vec3 kFloorNormal = vec3(0.0, 1.0, 0.0);
const float kFloorD = 0.0;

// Left red sphere (id = 1)
const vec3 kSphereLeftCenter = vec3(-1.2, 1.0, -3.5);
const float kSphereLeftRadius = 1.0;

// Glass sphere (id = 2)
const vec3 kGlassCenter = vec3(0.7, 1.0, -5.0);
const float kGlassRadius = 1.0;

// Mirror sphere (id = 3)
const vec3 kMirrorCenter = vec3(1.2, 0.7, -2.5);
const float kMirrorRadius = 0.7;

// -------- Analytic intersections --------
bool intersectPlane(vec3 ro, vec3 rd, vec3 n, float d, out Hit h, int matId) {
    float denom = dot(n, rd);
    if (abs(denom) < 1e-6) return false;
    float t = -(dot(n, ro) + d) / denom;
    if (t < uEPS) return false;
    h.t = t;
    h.p = ro + rd * t;
    h.n = n;
    h.mat = matId;
    return true;
}

bool intersectSphere(vec3 ro, vec3 rd, vec3 c, float r, out Hit h, int matId) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float c2 = dot(oc, oc) - r * r;
    float disc = b * b - c2;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float t = -b - s;
    if (t < uEPS) t = -b + s;
    if (t < uEPS) return false;
    h.t = t;
    h.p = ro + rd * t;
    h.n = normalize(h.p - c);
    h.mat = matId;
    return true;
}

bool traceAnalytic(vec3 ro, vec3 rd, out Hit hit) {
    hit.t = uINF;
    Hit h;

    if (intersectPlane(ro, rd, kFloorNormal, kFloorD, h, 0) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, kSphereLeftCenter, kSphereLeftRadius, h, 1) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, kGlassCenter, kGlassRadius, h, 2) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, kMirrorCenter, kMirrorRadius, h, 3) && h.t < hit.t) hit = h;

    return hit.t < uINF;
}

// Trace analytic scene but IGNORE the glass sphere (mat id 2).
// Used by glass shading to see "what's behind" the glass.
bool traceAnalyticIgnoreGlass(vec3 ro, vec3 rd, out Hit hit) {
    hit.t = uINF;
    Hit h;

    // Ground plane (mat 0)
    if (intersectPlane(ro, rd, vec3(0, 1, 0), 0.0, h, 0) && h.t < hit.t)
    hit = h;

    // Red sphere (mat 1)
    if (intersectSphere(ro, rd, vec3(-1.2, 1.0, -3.5), 1.0, h, 1) && h.t < hit.t)
    hit = h;

    // Glass sphere (mat 2) – SKIPPED on purpose

    // Mirror sphere (mat 3)
    if (intersectSphere(ro, rd, vec3(1.2, 0.7, -2.5), 0.7, h, 3) && h.t < hit.t)
    hit = h;

    return hit.t < uINF;
}

// -------- Sky ----------
vec3 sky(vec3 dir) {
    // If an environment cubemap is enabled, use it; otherwise fall back to analytic sky.
    if (uUseEnvMap == 1) {
        vec3 env = texture(uEnvMap, dir).rgb;
        return env * uEnvIntensity;
    }

    float t = clamp(0.5 * (dir.y + 1.0), 0.0, 1.0);
    vec3 col = mix(vec3(0.6, 0.7, 0.9) * 0.3,
                   vec3(0.1, 0.15, 0.3) * 0.3,
                   1.0 - t);
    return col;
}

#endif // RT_SCENE_ANALYTIC_GLSL