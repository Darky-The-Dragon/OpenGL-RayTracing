// rt_lighting.glsl
#ifndef RT_LIGHTING_GLSL
#define RT_LIGHTING_GLSL

/*
    rt_lighting.glsl – Direct & Indirect Lighting for Analytic and BVH Scenes

    This module defines:
    - A simple disk area light (kLightCenter, kLightN, kLightRadius, kLightCol).
    - Unified occlusion tests that work in both analytic and BVH modes.
    - A shared Lambert + Phong BRDF helper.
    - Sun, sky, and point lights (hybrid analytic lights shared across scenes).
    - Direct lighting evaluators:
        * directLight()      – analytic scene (plane + spheres)
        * directLightBVH()   – BVH triangle scene
    - One-bounce diffuse GI for analytic and BVH scenes with basic clamping.
    - Glass shading with thin refraction and local reflections.
    - Mirror shading using analytic scene traces.
    - Ambient occlusion (AO) using cosine-weighted hemisphere sampling.

    All routines assume the presence of:
    - uUseBVH, uAO_* uniforms.
    - traceAnalytic(), traceAnalyticIgnoreGlass(), traceAnalyticIgnorePointLight().
    - traceBVH(), traceBVHShadow().
    - MaterialProps, sky(), sampleHemisphereCosine(), etc.
*/

// ------------- Disk area light --------------
const vec3 kLightCenter = vec3(0.0, 5.0, -3.0);
const vec3 kLightN = normalize(vec3(0.0, -1.0, 0.2));
const float kLightRadius = 1.2;
const vec3 kLightCol = vec3(18.0);

// ---------------------------------------------------------------------------
// Unified shadow test for both modes
// ---------------------------------------------------------------------------

/**
 * @brief Tests whether the segment between p and q is occluded.
 *
 * Builds a shadow ray from p toward q and checks:
 *  - BVH scene via traceBVHShadow, or
 *  - Analytic scene via traceAnalytic.
 *
 * @param p Start position (usually a surface point).
 * @param q Target point (e.g., area-light point).
 * @return True if any geometry blocks the path before reaching q.
 */
bool occludedToward(vec3 p, vec3 q) {
    vec3 rd = normalize(q - p);
    float maxT = length(q - p);
    float eps = epsForDist(maxT);
    if (uUseBVH == 1) {
        return traceBVHShadow(p + rd * eps, rd, maxT - eps);
    } else {
        Hit h;
        if (traceAnalytic(p + rd * eps, rd, h) && h.t < maxT - eps) return true;
        return false;
    }
}

// ---------------------------------------------------------------------------
// Shared BRDF helper: Lambert + optional Phong spec
// ---------------------------------------------------------------------------

/**
 * @brief Evaluates a Lambert + Phong BRDF for a single light sample.
 *
 * @param N           Surface normal (world space, normalized).
 * @param V           View direction (from surface → camera).
 * @param L           Light direction (from surface → light).
 * @param Li          Incident radiance from the light.
 * @param albedo      Diffuse albedo color.
 * @param specStrength Scalar multiplier for the specular lobe.
 * @param gloss       Phong exponent controlling highlight sharpness.
 * @return Outgoing radiance contribution from this light sample.
 */
vec3 shadeLambertPhong(
    vec3 N, vec3 V, vec3 L, vec3 Li,
    vec3 albedo, float specStrength, float gloss)
{
    float ndl = max(dot(N, L), 0.0);
    if (ndl <= 0.0) return vec3(0.0);

    // Diffuse
    vec3 diffuse = albedo * (ndl / uPI);

    // Specular (Phong)
    vec3 spec = vec3(0.0);
    if (specStrength > 0.0) {
        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float phong = pow(ndh, gloss);
        spec = specStrength * phong * vec3(1.0);
    }

    return (diffuse + spec) * Li;
}

// ---------------------------------------------------------------------------
// Sun light (directional, shadowed)
// ---------------------------------------------------------------------------

/**
 * @brief Directional sunlight contribution with hard shadows.
 *
 * Uses the shared BRDF helper for non-glass/non-mirror materials.
 *
 * @param h    Primary hit information.
 * @param mat  Material properties at the hit.
 * @param Vdir Direction from hit → camera.
 * @return Radiance contribution from the sun.
 */
vec3 sunDirect(Hit h, MaterialProps mat, vec3 Vdir)
{
    if (uSunEnabled == 0) return vec3(0.0);

    vec3 N = normalize(h.n);
    vec3 V = normalize(Vdir);
    vec3 L = normalize(-uSunDir); // light dir from hit -> sun

    float ndl = max(dot(N, L), 0.0);
    if (ndl <= 0.0) return vec3(0.0);

    // Shadow ray toward sun (approx "infinite" distance)
    float maxT = 1000.0;
    float eps = epsForDist(maxT);
    vec3 origin = h.p + N * eps;

    bool blocked;
    if (uUseBVH == 1) {
        blocked = traceBVHShadow(origin, L, maxT - eps);
    } else {
        Hit tmp;
        blocked = traceAnalytic(origin, L, tmp);
    }
    if (blocked) return vec3(0.0);

    vec3 Li = uSunColor * uSunIntensity;

    // Only non-mirror/non-glass get Phong spec
    float specStrength = (mat.type == 0) ? mat.specStrength : 0.0;
    return shadeLambertPhong(N, V, L, Li, mat.albedo, specStrength, mat.gloss);
}

// ---------------------------------------------------------------------------
// Sky dome (ambient-ish, no extra shadows; AO will handle occlusion feel)
// ---------------------------------------------------------------------------

/**
 * @brief Simple hemispherical sky dome contribution.
 *
 * Approximates ambient light from a single sky direction uSkyUpDir with
 * cosine-weighting. Shadows are handled separately via AO.
 */
vec3 skyDirect(Hit h, MaterialProps mat, vec3 Vdir)
{
    if (uSkyEnabled == 0) return vec3(0.0);

    vec3 N = normalize(h.n);
    vec3 U = normalize(uSkyUpDir);

    float ndl = max(dot(N, U), 0.0);
    if (ndl <= 0.0) return vec3(0.0);

    // Simple cosine-weighted dome around U
    vec3 Li = uSkyColor * uSkyIntensity;
    return mat.albedo * (ndl / uPI) * Li; // diffuse only
}

// ---------------------------------------------------------------------------
// Point light (shadowed, inverse-square falloff)
// ---------------------------------------------------------------------------

/**
 * @brief Point light contribution with inverse-square falloff and shadows.
 *
 * For the analytic scene, the emissive point-light marker sphere is excluded
 * from shadow tests via traceAnalyticIgnorePointLight().
 */
vec3 pointDirect(Hit h, MaterialProps mat, vec3 Vdir)
{
    if (uPointLightEnabled == 0) return vec3(0.0);

    vec3 N = normalize(h.n);
    vec3 V = normalize(Vdir);
    vec3 toL = uPointLightPos - h.p;
    float dist2 = dot(toL, toL);
    if (dist2 <= 1e-6) return vec3(0.0);

    float dist = sqrt(dist2);
    vec3 L = toL / dist;
    float ndl = max(dot(N, L), 0.0);
    if (ndl <= 0.0) return vec3(0.0);

    float eps = epsForDist(dist);
    vec3 origin = h.p + L * eps;

    bool blocked;
    if (uUseBVH == 1) {
        blocked = traceBVHShadow(origin, L, dist - eps);
    } else {
        Hit tmp;
        // IMPORTANT: do NOT let the marker sphere shadow its own light
        blocked = traceAnalyticIgnorePointLight(origin, L, tmp) && tmp.t < dist - eps;
    }
    if (blocked) return vec3(0.0);

    // Inverse-square falloff
    vec3 Li = uPointLightColor * (uPointLightIntensity / max(dist2, 1e-4));

    float specStrength = (mat.type == 0) ? mat.specStrength : 0.0;
    return shadeLambertPhong(N, V, L, Li, mat.albedo, specStrength, mat.gloss);
}

// ============================================================================
// Basis & sampling utilities
// ============================================================================

/**
 * @brief Builds an orthonormal basis (T, B, N) from a normal.
 *
 * @param N  Normalized surface normal.
 * @param T  Output tangent vector.
 * @param B  Output bitangent vector.
 */
void buildONB(in vec3 N, out vec3 T, out vec3 B) {
    vec3 up = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

/**
 * @brief Builds a local frame (t,b) around the disk light normal kLightN.
 *
 * Used to map 2D disk samples into world space for the soft area light.
 */
void buildLightFrame(out vec3 t, out vec3 b) {
    t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0))
                  : cross(kLightN, vec3(1, 0, 0)));
    b = cross(kLightN, t);
}

/**
 * @brief Cosine-weighted hemisphere sample around normal N.
 *
 * @param N Surface normal (world space).
 * @param u 2D random sample in [0,1]^2.
 * @return Sampled direction on the hemisphere around N.
 */
vec3 sampleHemisphereCosine(vec3 N, vec2 u) {
    // cosine-weighted sample in local space (around +Y)
    float phi = 2.0 * uPI * u.x;
    float r = sqrt(u.y);
    float x = r * cos(phi);
    float z = r * sin(phi);
    float y = sqrt(max(0.0, 1.0 - u.y));
    vec3 l = vec3(x, y, z); // local (+Y hemisphere)

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

/**
 * @brief Computes a per-pixel offset for disk-light sampling.
 *
 * Combines hash-based noise and a low-discrepancy sequence to rotate the
 * sample pattern over time and across pixels, reducing structured noise.
 */
vec2 cpOffset(vec2 pix, int frame) {
    // Per-pixel hash → [0,1)^2
    vec2 h = vec2(
    rand(pix, frame * 911),
    rand(pix.yx, frame * 577)
    );
    // Per-frame low-discrepancy rotation
    vec2 ld = ld2(frame);
    return fract(h + ld);
}

/**
 * @brief Standalone Phong specular evaluation helper.
 *
 * Used by some direct-light routines when a separate specular term is needed.
 */
vec3 evalPhongSpec(vec3 N, vec3 V, vec3 L, float gloss, float specStrength) {
    if (specStrength <= 0.0) return vec3(0.0);
    vec3 H = normalize(L + V);
    float ndh = max(dot(N, H), 0.0);
    float phong = pow(ndh, gloss);
    return specStrength * phong * vec3(1.0);
}

/**
 * @brief Direct lighting for the analytic scene (plane + spheres).
 *
 * IMPORTANT:
 *  - For primary hits of mirror/glass we use shadeMirror/shadeGlass in rt.frag.
 *  - This function is also used for secondary hits (reflections / refractions).
 *    For those, we approximate mirror/glass locally here WITHOUT calling
 *    shadeMirror/shadeGlass again, to avoid recursion.
 */
vec3 directLight(Hit h, int frame, vec3 Vdir) {
    vec3 N = normalize(h.n);
    vec3 sum = vec3(0.0);

    MaterialProps mat = getMaterial(h.mat);
    vec3 V = normalize(Vdir); // from hit → camera

    // --------------------------------------------------------------------
    // Special handling for mirror / glass on SECONDARY hits
    // (called from shadeGlass/shadeMirror or GI). We approximate them as
    // reflective here so they don't appear as flat albedo.
    // --------------------------------------------------------------------
    if (mat.type == 1) {
        // Mirror-like: sample env/sky along perfect reflection, tinted.
        vec3 R = reflect(-V, N);
        vec3 col;
        if (uUseEnvMap == 1) {
            col = texture(uEnvMap, R).rgb * uEnvIntensity;
        } else {
            col = sky(R);
        }
        return col * mat.albedo;
    }

    if (mat.type == 2) {
        // Glass-like: mostly reflected env, plus some sky dome diffuse,
        // so it doesn't turn into flat color.
        vec3 R = reflect(-V, N);
        vec3 refl;
        if (uUseEnvMap == 1) {
            refl = texture(uEnvMap, R).rgb * uEnvIntensity;
        } else {
            refl = sky(R);
        }

        vec3 skyDiff = skyDirect(h, mat, V);
        return refl * mat.albedo + skyDiff;
    }

    // --------------------------------------------------------------------
    // Regular diffuse / Phong materials (type == 0)
    // --------------------------------------------------------------------
    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0))
                       : cross(kLightN, vec3(1, 0, 0)));
    vec3 b = cross(kLightN, t);

    // Per-pixel rotated sampling
    vec2 rot = cpOffset(gl_FragCoord.xy, uFrameIndex);

    // Soft disk area light
    for (int i = 0; i < SOFT_SHADOW_SAMPLES; ++i) {
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(i), frame),
        rand(gl_FragCoord.yx + float(31 * i + 7), frame)
        );
        u = fract(u + rot);

        vec2 d = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t * d.x + b * d.y;

        vec3 L = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0);
        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);

        float geom = (ndl * cosThetaL) / r2;
        float vis = occludedToward(h.p, xL) ? 0.0 : 1.0;

        vec3 Li = kLightCol * geom * vis;

        float specStrength = mat.specStrength;
        sum += shadeLambertPhong(N, V, L, Li, mat.albedo, specStrength, mat.gloss);
    }

    sum /= float(SOFT_SHADOW_SAMPLES);

    // Hybrid lights
    sum += sunDirect(h, mat, V);
    sum += skyDirect(h, mat, V);
    sum += pointDirect(h, mat, V);

    return sum;
}

// ---- Direct lighting for BVH triangles (simple white plastic) + hybrid lights

/**
 * @brief Direct lighting for BVH triangle geometry.
 *
 * Uses a hard-coded "white plastic" material for the triangle mesh and
 * reuses the same disk, sun, sky, and point lights as the analytic scene.
 */
vec3 directLightBVH(Hit h, int frame, vec3 Vdir) {
    vec3 N = normalize(h.n);
    vec3 sum = vec3(0.0);

    // Hardcoded BVH material: white plastic
    const vec3 albedo = vec3(0.85);
    const float specStrength = 0.25;
    const float gloss = 32.0;

    vec3 t = normalize(abs(kLightN.y) < 0.99 ? cross(kLightN, vec3(0, 1, 0))
                       : cross(kLightN, vec3(1, 0, 0)));
    vec3 b = cross(kLightN, t);

    vec2 rot = cpOffset(gl_FragCoord.xy, uFrameIndex);
    vec3 V = normalize(Vdir);

    // Disk area light
    for (int i = 0; i < SOFT_SHADOW_SAMPLES; ++i) {
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(i), frame),
        rand(gl_FragCoord.yx + float(31 * i + 7), frame)
        );
        u = fract(u + rot);

        vec2 d = concentricSample(u) * kLightRadius;
        vec3 xL = kLightCenter + t * d.x + b * d.y;

        vec3 L = normalize(xL - h.p);
        float ndl = max(dot(N, L), 0.0);
        float cosThetaL = max(dot(-kLightN, L), 0.0);
        float r2 = max(dot(xL - h.p, xL - h.p), 1e-4);

        float geom = (ndl * cosThetaL) / r2;
        float vis = occludedToward(h.p, xL) ? 0.0 : 1.0;

        vec3 Li = kLightCol * geom * vis;

        sum += shadeLambertPhong(N, V, L, Li, albedo, specStrength, gloss);
    }

    sum /= float(SOFT_SHADOW_SAMPLES);

    // Approximate analytic MaterialProps for hybrid lights
    MaterialProps fakeMat;
    fakeMat.albedo = albedo;
    fakeMat.specStrength = specStrength;
    fakeMat.gloss = gloss;
    fakeMat.type = 0;      // diffuse-ish
    fakeMat.ior = 1.0;

    sum += sunDirect(h, fakeMat, V);
    sum += skyDirect(h, fakeMat, V);
    sum += pointDirect(h, fakeMat, V);

    return sum;
}

// ============================================================================
// One-bounce diffuse GI (indirect lighting)
// ============================================================================

/**
 * @brief One-bounce diffuse GI for the analytic scene.
 *
 * Shoots a single cosine-weighted bounce from the primary hit, then:
 *  - if it hits: computes direct lighting at the secondary point
 *  - if it misses: samples the sky
 */
vec3 oneBounceGIAnalytic(Hit h0, int frame, int seed) {
    MaterialProps mat0 = getMaterial(h0.mat);
    vec3 albedo0 = mat0.albedo;

    vec3 N0 = normalize(h0.n);

    // Random sample on hemisphere (per-pixel, per-seed)
    vec2 u = vec2(
    rand(gl_FragCoord.xy + float(seed * 13), frame),
    rand(gl_FragCoord.yx + float(seed * 37), frame)
    );

    vec3 wi = sampleHemisphereCosine(N0, u);
    float cosTheta = max(dot(N0, wi), 0.0);
    if (cosTheta <= 0.0) return vec3(0.0);

    // Trace from the hit using a normal-based offset (more robust than along wi)
    vec3 origin = h0.p + N0 * uEPS;

    Hit h1;
    bool hit1 = traceAnalytic(origin, wi, h1);

    vec3 Li;
    if (hit1) {
        // Direct lighting at the secondary point (includes all lights)
        vec3 V1 = -wi;
        Li = directLight(h1, frame, V1);
    } else {
        // Bounce to sky
        Li = sky(wi);
    }

    // Lambertian throughput: albedo0 * (cosTheta / uPI)
    return albedo0 * (cosTheta / uPI) * Li;
}

/**
 * @brief One-bounce diffuse GI for BVH geometry with firefly clamping.
 *
 * Uses a cosine-weighted bounce, reuses directLightBVH at the secondary hit,
 * and applies a luminance clamp to reduce extreme GI spikes.
 */
vec3 oneBounceGIBVH(Hit h0, int frame, int seed) {
    // Hard-coded BVH albedo (same spirit as directLightBVH)
    const vec3 albedo0 = vec3(0.85);
    const float MAX_GI_LUM = 8.0;   // tweak: 4–12 depending on light power
    const float MIN_COS_THETA = 0.1;   // avoid super-grazing bounces

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
    vec3 origin = h0.p + N0 * uEPS;

    Hit h1;
    bool hit1 = traceBVH(origin, wi, h1);

    vec3 Li;
    if (hit1) {
        vec3 V1 = -wi;
        // Includes disk, sky directional, and point light
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

// ============================================================================
// Glass shading – soft thin refraction with local reflections
// ============================================================================
// wo = direction from hit -> camera (i.e. -rayDir), frame = random seed

/**
 * @brief Shading for glass materials in the analytic scene.
 *
 * Approximates:
 *  - Thin refraction (straight-through + softened bent refraction).
 *  - Local and environment reflections.
 *  - Fresnel blending between reflection and refraction using Schlick's approx.
 */
vec3 shadeGlass(const Hit h, const vec3 wo, const MaterialProps mat, int frame) {
    vec3 N = normalize(h.n);
    vec3 V = normalize(wo);   // hit -> camera
    vec3 I = -V;              // camera -> hit

    float ior = mat.ior;
    float eta = 1.0 / max(ior, 1.0001);  // air -> glass

    // 0.0 = no distortion (just straight-through)
    // 1.0 = fully physical refraction
    const float distortionStrength = 0.45;

    // ------------------------------------------------------------------------
    // REFLECTION: env + local scene
    // ------------------------------------------------------------------------
    vec3 R = reflect(I, N);

    // Env reflection (cubemap/sky)
    vec3 reflectEnv = sky(R);

    // Local reflection: what the sphere "sees" around it
    vec3 reflectLocal = reflectEnv;
    {
        Hit hRefl;
        if (traceAnalyticIgnoreGlass(h.p + R * uEPS, R, hRefl)) {
            vec3 V2 = normalize(uCamPos - hRefl.p);   // hit -> camera
            reflectLocal = directLight(hRefl, frame, V2);
        }
    }

    // Blend env + local so nearby objects show up as small highlights,
    // but the sphere doesn't turn into a full-on mirror.
    const float localReflWeight = 0.4;    // tweak 0.2–0.6
    vec3 reflectCol = mix(reflectEnv, reflectLocal, localReflWeight);

    // ------------------------------------------------------------------------
    // REFRACTION: straight-through + softened bent refraction
    // ------------------------------------------------------------------------

    // Straight-through color (no bending – acts like thin glass)
    vec3 straightCol = vec3(0.0);
    {
        Hit hStraight;
        if (traceAnalyticIgnoreGlass(h.p + I * uEPS, I, hStraight)) {
            vec3 V2 = normalize(uCamPos - hStraight.p);  // from hit -> camera
            straightCol = directLight(hStraight, frame, V2);
        } else {
            straightCol = sky(I);
        }
    }

    // Bent refraction color (physical direction, blended down)
    float cosTheta = clamp(dot(-I, N), 0.0, 1.0);
    float k = 1.0 - eta * eta * (1.0 - cosTheta * cosTheta);

    vec3 refrCol = straightCol;  // fallback: at worst, just see straight through

    if (distortionStrength > 0.0 && k > 0.0) {
        // Physical refraction direction
        vec3 T_phys = normalize(refract(I, N, eta));

        // Soften the effect to avoid crazy magnification
        vec3 T = normalize(mix(I, T_phys, distortionStrength));

        Hit hRefr;
        vec3 bentCol;
        if (traceAnalyticIgnoreGlass(h.p + T * uEPS, T, hRefr)) {
            vec3 V2 = normalize(uCamPos - hRefr.p);
            bentCol = directLight(hRefr, frame, V2);
        } else {
            bentCol = sky(T);
        }

        refrCol = mix(straightCol, bentCol, distortionStrength);
    }

    // Tint by glass albedo
    refrCol *= mat.albedo;

    // ------------------------------------------------------------------------
    // Fresnel (Schlick) – mix reflection vs refraction
    // ------------------------------------------------------------------------
    float F0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

    // Center = mostly refraction, rim = more reflection
    return mix(refrCol, reflectCol, fresnel);
}

// ============================================================================
// Mirror shading helper – analytic scene only
// ============================================================================

/**
 * @brief Perfect mirror shading for analytic scene materials.
 *
 * Casts a reflection ray into the analytic scene, optionally adds one-bounce
 * GI at the reflected hit, and falls back to the environment if nothing is hit.
 */
vec3 shadeMirror(const Hit h, const vec3 wo, const MaterialProps mat, int frame) {
    vec3 N = normalize(h.n);
    vec3 I = -normalize(wo);       // direction from hit → camera
    vec3 R = reflect(I, N);        // perfect mirror reflection
    vec3 org = h.p + R * uEPS;

    Hit h2;
    bool hit2 = traceAnalytic(org, R, h2);

    vec3 col;
    if (hit2) {
        // Direct lighting at the reflected hit (all lights)
        vec3 V2 = -R;
        col = directLight(h2, frame, V2);

        // Optional one-bounce GI for the reflected point
        if (uEnableGI == 1) {
            int giSeed = frame * 131 + 17;
            col += uGiScaleAnalytic * oneBounceGIAnalytic(h2, frame, giSeed);
        }
    } else {
        // Fallback: environment or sky
        if (uUseEnvMap == 1) {
            col = texture(uEnvMap, R).rgb * uEnvIntensity;
        } else {
            col = sky(R);
        }
    }

    // Apply mirror tint
    col *= mat.albedo;

    return col;
}

// ---------------------------------------------------------------------------
// Ambient Occlusion (AO)
// ---------------------------------------------------------------------------

/**
 * @brief Computes ambient occlusion factor around a hit point.
 *
 * Shoots several hemisphere rays around the normal and counts the fraction
 * of rays that quickly hit geometry within a radius. The final AO factor
 * is clamped and remapped to avoid fully black regions.
 */
float computeAO(Hit h, int frame) {
    vec3 N = normalize(h.n);
    int occludedCount = 0;

    for (int i = 0; i < uAO_SAMPLES; ++i) {
        // deterministic but decorrelated per-pixel/per-sample noise
        vec2 u = vec2(
        rand(gl_FragCoord.xy + float(37 * i + 3), frame),
        rand(gl_FragCoord.yx + float(19 * i + 11), frame)
        );

        // cosine-weighted world-space direction around N
        vec3 dir = sampleHemisphereCosine(N, u);

        // ray origin slightly above the surface (offset along normal for robustness)
        vec3 org = h.p + N * uAO_BIAS;

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
    float ao = 1.0 - occ;                                 // 1=open, 0=closed

    // Keep a minimum so nothing goes pitch-black
    ao = clamp(mix(uAO_MIN, 1.0, ao), uAO_MIN, 1.0);

    return ao;
}

#endif // RT_LIGHTING_GLSL