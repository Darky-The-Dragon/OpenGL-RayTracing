// rt_lighting.glsl
#ifndef RT_LIGHTING_GLSL
#define RT_LIGHTING_GLSL

// ------------- Light --------------
const vec3  kLightCenter = vec3(0.0, 5.0, -3.0);
const vec3  kLightN      = normalize(vec3(0.0, -1.0, 0.2));
const float kLightRadius = 1.2;
const vec3  kLightCol    = vec3(18.0);

// ---- Unified shadow test for both modes
bool occludedToward(vec3 p, vec3 q) {
    vec3 rd   = normalize(q - p);
    float maxT = length(q - p);
    float eps  = epsForDist(maxT);
    if (uUseBVH == 1) {
        return traceBVHShadow(p + rd * eps, rd, maxT - eps);
    } else {
        Hit h;
        if (traceAnalytic(p + rd * eps, rd, h) && h.t < maxT - eps) return true;
        return false;
    }
}

// ============================================================================
// Basis & sampling utilities
// ============================================================================

// Build an orthonormal basis (T,B,N) from a normal
void buildONB(in vec3 N, out vec3 T, out vec3 B) {
    vec3 up = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// Cosine-weighted hemisphere sample around normal N using shared helpers.
// u in [0,1]^2.
vec3 sampleHemisphereCosine(vec3 N, vec2 u) {
    // cosine-weighted sample in local space (around +Y)
    float phi = 2.0 * uPI * u.x;
    float r   = sqrt(u.y);
    float x   = r * cos(phi);
    float z   = r * sin(phi);
    float y   = sqrt(max(0.0, 1.0 - u.y));
    vec3 l    = vec3(x, y, z); // local (+Y hemisphere)

    // build tangent frame once
    vec3 T, B;
    buildONB(normalize(N), T, B);

    // Map local sample into world space oriented around N
    return normalize(l.x * T + l.z * B + l.y * N);
}

// ============================================================================
// Direct lighting
// ============================================================================

// ---- Direct lighting (analytic & BVH) with per-pixel CP rotation + frame LD rotation
vec2 cpOffset(vec2 pix, int frame) {
    // Per-pixel hash → [0,1)^2
    vec2 h = vec2(
    rand(pix,    frame * 911),
    rand(pix.yx, frame * 577)
    );
    // Per-frame low-discrepancy rotation
    vec2 ld = ld2(frame);
    return fract(h + ld);
}

// ---- Direct lighting (analytic) with cheap specular
vec3 directLight(Hit h, int frame, vec3 Vdir) {
    vec3 N = normalize(h.n);
    vec3 sum = vec3(0.0);

    MaterialProps mat = getMaterial(h.mat);

    // Orthonormal basis for disk light
    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0))
                       : cross(kLightN, vec3(1, 0, 0)));
    vec3 b = cross(kLightN, t);

    // Per-pixel rotated sampling like before
    vec2 rot = cpOffset(gl_FragCoord.xy, uFrameIndex);

    vec3 V = normalize(Vdir); // from hit → camera

    for (int i = 0; i < SOFT_SHADOW_SAMPLES; ++i) {
        // base random
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(i),          frame),
        rand(gl_FragCoord.yx + float(31 * i + 7), frame)
        );
        // Cranley–Patterson rotation + wrap
        u = fract(u + rot);

        vec2 d = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t * d.x + b * d.y;

        vec3 L   = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0);
        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);

        float geom = (ndl * cosThetaL) / r2;
        float vis  = occludedToward(h.p, xL) ? 0.0 : 1.0;

        // Incoming radiance from the disk
        vec3 Li = kLightCol * geom * vis;

        // Diffuse (Lambert)
        vec3 diffuse = mat.albedo * (ndl / uPI);

        // Cheap Phong specular (only for non-mirror mats)
        vec3 spec = vec3(0.0);
        if (mat.type == 0 && mat.specStrength > 0.0) {
            vec3 H = normalize(L + V);               // halfway vector
            float ndh = max(dot(N, H), 0.0);
            float phong = pow(ndh, mat.gloss);
            spec = mat.specStrength * phong * vec3(1.0);
        }

        sum += (diffuse + spec) * Li;
    }

    return sum / float(SOFT_SHADOW_SAMPLES);
}

// ---- Direct lighting for BVH triangles (simple white plastic)
vec3 directLightBVH(Hit h, int frame, vec3 Vdir) {
    vec3 N = normalize(h.n);
    vec3 sum = vec3(0.0);

    // Hardcoded BVH material: white plastic
    const vec3  albedo       = vec3(0.85);
    const float specStrength = 0.25;
    const float gloss        = 32.0;

    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0))
                       : cross(kLightN, vec3(1, 0, 0)));
    vec3 b = cross(kLightN, t);

    vec2 rot = cpOffset(gl_FragCoord.xy, uFrameIndex);
    vec3 V   = normalize(Vdir);

    for (int i = 0; i < SOFT_SHADOW_SAMPLES; ++i) {
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(i),          frame),
        rand(gl_FragCoord.yx + float(31 * i + 7), frame)
        );
        u = fract(u + rot);

        vec2 d = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t * d.x + b * d.y;

        vec3 L   = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0);
        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);

        float geom = (ndl * cosThetaL) / r2;
        float vis  = occludedToward(h.p, xL) ? 0.0 : 1.0;

        vec3 Li = kLightCol * geom * vis;

        vec3 diffuse = albedo * (ndl / uPI);

        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float phong = pow(ndh, gloss);
        vec3 spec = specStrength * phong * vec3(1.0);

        sum += (diffuse + spec) * Li;
    }
    return sum / float(SOFT_SHADOW_SAMPLES);
}

// ============================================================================
// One-bounce diffuse GI (indirect lighting)
// ============================================================================

// Analytic scene: one diffuse bounce from primary hit h0
vec3 oneBounceGIAnalytic(Hit h0, int frame, int seed) {
    MaterialProps mat0 = getMaterial(h0.mat);
    vec3 albedo0 = mat0.albedo;

    // Random sample on hemisphere (per-pixel, per-seed)
    vec2 u = vec2(
    rand(gl_FragCoord.xy + float(seed * 13), frame),
    rand(gl_FragCoord.yx + float(seed * 37), frame)
    );

    vec3 wi = sampleHemisphereCosine(normalize(h0.n), u);
    float cosTheta = max(dot(normalize(h0.n), wi), 0.0);
    if (cosTheta <= 0.0) return vec3(0.0);

    // Trace from the hit along wi
    Hit h1;
    bool hit1 = traceAnalytic(h0.p + wi * uEPS, wi, h1);

    vec3 Li;
    if (hit1) {
        // Direct lighting at the secondary point
        vec3 V1 = -wi;
        Li = directLight(h1, frame, V1);
    } else {
        // Bounce to sky
        Li = sky(wi);
    }

    // Lambertian throughput: albedo0 * (cosTheta / uPI)
    return albedo0 * (cosTheta / uPI) * Li;
}

// BVH scene: one diffuse bounce from primary triangle hit, with clamping
vec3 oneBounceGIBVH(Hit h0, int frame, int seed)
{
    // Hard-coded BVH albedo (same spirit as directLightBVH)
    const vec3  albedo0         = vec3(0.85);
    const float MAX_GI_LUM      = 8.0;   // tweak: 4–12 depending on light power
    const float MIN_COS_THETA   = 0.1;   // avoid super-grazing bounces

    // Random sample on hemisphere (per-pixel, per-seed)
    vec2 u = vec2(
    rand(gl_FragCoord.xy + float(seed * 19), frame),
    rand(gl_FragCoord.yx + float(seed * 41), frame)
    );

    vec3 N0 = normalize(h0.n);
    vec3 wi = sampleHemisphereCosine(N0, u);   // cosine-weighted around N
    float cosTheta = max(dot(N0, wi), 0.0);

    // Discard very grazing bounces (they cause huge variance on tiny triangles)
    if (cosTheta <= MIN_COS_THETA)
    return vec3(0.0);

    // IMPORTANT: offset along the surface normal, not along wi
    // This is more robust for thin triangles / sharp corners.
    vec3 origin = h0.p + N0 * uEPS;

    Hit  h1;
    bool hit1 = traceBVH(origin, wi, h1);

    vec3 Li;
    if (hit1) {
        vec3 V1 = -wi;
        Li = directLightBVH(h1, frame, V1);
    } else {
        Li = sky(wi);
    }

    // Raw Lambertian contribution
    vec3 contrib = albedo0 * (cosTheta / uPI) * Li;

    // Luminance-based clamp to kill fireflies
    float lum = dot(contrib, vec3(0.299, 0.587, 0.114));
    if (lum > MAX_GI_LUM) {
        float s = MAX_GI_LUM / max(lum, 1e-6);
        contrib *= s;
    }

    return contrib;
}

// ---------------------------------------------------------------------------
// Ambient Occlusion (AO)
// Returns a factor in [0,1] (1 = fully open, 0 = fully occluded).
// Designed to be *subtle* so it doesn't nuke the lighting.
// ---------------------------------------------------------------------------
float computeAO(Hit h, int frame)
{
    vec3 N = normalize(h.n);
    int occludedCount = 0;

    for (int i = 0; i < uAO_SAMPLES; ++i) {
        // deterministic but decorrelated per-pixel/per-sample noise
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(37 * i + 3),  frame),
        rand(gl_FragCoord.yx + float(19 * i + 11), frame)
        );

        // cosine-weighted world-space direction around N
        vec3 dir = sampleHemisphereCosine(N, u);

        // ray origin slightly above the surface
        vec3 org = h.p + dir * uAO_BIAS;

        Hit tmp;
        bool hitAny =
        (uUseBVH == 1)
        ? traceBVH(org, dir, tmp)
        : traceAnalytic(org, dir, tmp);

        // count as occluded only if something is reasonably close
        if (hitAny && tmp.t < uAO_RADIUS) {
            occludedCount++;
        }
    }

    float occ = float(occludedCount) / float(uAO_SAMPLES); // 0..1
    float ao  = 1.0 - occ;                                // 1=open, 0=closed

    // Keep a minimum so nothing goes pitch-black
    ao = clamp(mix(uAO_MIN, 1.0, ao), uAO_MIN, 1.0);

    return ao;
}

#endif // RT_LIGHTING_GLSL