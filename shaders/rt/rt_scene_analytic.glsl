// rt_scene_analytic.glsl
#ifndef RT_SCENE_ANALYTIC_GLSL
#define RT_SCENE_ANALYTIC_GLSL

// -------- Analytic intersections --------
bool intersectPlane(vec3 ro, vec3 rd, vec3 n, float d, out Hit h, int matId) {
    float denom = dot(n, rd);
    if (abs(denom) < 1e-6) return false;
    float t = -(dot(n, ro) + d) / denom;
    if (t < EPS) return false;
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
    if (t < EPS) t = -b + s;
    if (t < EPS) return false;
    h.t = t;
    h.p = ro + rd * t;
    h.n = normalize(h.p - c);
    h.mat = matId;
    return true;
}

bool traceAnalytic(vec3 ro, vec3 rd, out Hit hit) {
    hit.t = INF;
    Hit h;
    if (intersectPlane(ro, rd, vec3(0, 1, 0), 0.0, h, 0) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, vec3(-1.2, 1.0, -3.5), 1.0, h, 1) && h.t < hit.t) hit = h;
    if (intersectSphere(ro, rd, vec3(1.2, 0.7, -2.5), 0.7, h, 3) && h.t < hit.t) hit = h;
    return hit.t < INF;
}

// -------- Sky ----------
vec3 sky(vec3 d) {
    float t = clamp(0.5 * (d.y + 1.0), 0.0, 1.0);
    vec3 col = mix(vec3(0.6, 0.7, 0.9) * 0.3,
                   vec3(0.1, 0.15, 0.3) * 0.3,
                   1.0 - t);
    return col;
}

#endif // RT_SCENE_ANALYTIC_GLSL