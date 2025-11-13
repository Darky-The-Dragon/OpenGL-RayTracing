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
        vec3 diffuse = mat.albedo * (ndl / PI);

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

        vec3 diffuse = albedo * (ndl / PI);

        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float phong = pow(ndh, gloss);
        vec3 spec = specStrength * phong * vec3(1.0);

        sum += (diffuse + spec) * Li;
    }
    return sum / float(SOFT_SHADOW_SAMPLES);
}

#endif // RT_LIGHTING_GLSL