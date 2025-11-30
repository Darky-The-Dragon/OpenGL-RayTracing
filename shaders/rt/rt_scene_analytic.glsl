// rt_scene_analytic.glsl
#ifndef RT_SCENE_ANALYTIC_GLSL
#define RT_SCENE_ANALYTIC_GLSL

/*
    rt_scene_analytic.glsl – Analytic Test Scene & Sky

    This module defines a small, hard-coded “analytic” scene used by the
    ray tracer. All geometry is defined directly in GLSL:

      - Infinite plane (floor).
      - Left diffuse sphere (GUI-controlled material).
      - Glass sphere.
      - Mirror sphere.
      - Small marker sphere placed at the point light position.

    It provides:
      - Basic plane and sphere intersection routines.
      - A traceAnalyticCore() function that can optionally ignore:
          * the glass sphere (for glass rays),
          * the point-light marker sphere (for shadow rays to the bulb).
      - Three public trace functions:
          * traceAnalytic()                → full scene
          * traceAnalyticIgnoreGlass()     → no glass sphere
          * traceAnalyticIgnorePointLight()→ no point-light marker sphere
      - A sky() function that samples either:
          * an environment cubemap (if enabled), or
          * a simple analytic gradient sky (fallback).

    The material IDs (MAT_*) are defined in rt_materials.glsl and must
    stay consistent with this setup.
*/

// -------- Analytic scene constants (shared with glass shading) --------

// Infinite floor: normal and plane offset d (n·x + d = 0).
const vec3 kFloorNormal = vec3(0.0, 1.0, 0.0);
const float kFloorD = 0.0;

// Left diffuse sphere (MAT_ALBEDO_SPHERE)
const vec3 kSphereLeftCenter = vec3(-1.2, 1.0, -3.5);
const float kSphereLeftRadius = 1.0;

// Glass sphere (MAT_GLASS_SPHERE)
const vec3 kGlassCenter = vec3(0.7, 1.0, -5.0);
const float kGlassRadius = 1.0;

// Mirror sphere (MAT_MIRROR_SPHERE)
const vec3 kMirrorCenter = vec3(1.2, 0.7, -2.5);
const float kMirrorRadius = 0.7;

// Point light marker sphere (MAT_POINTLIGHT_SPHERE)
// Center is uPointLightPos; radius is only used for analytic intersection.
const float kPointLightRadius = 0.15;

// -------- Analytic intersections --------

/**
 * @brief Intersects a ray with an infinite plane.
 *
 * Plane equation: dot(n, x) + d = 0
 *
 * @param ro    Ray origin.
 * @param rd    Ray direction (assumed normalized).
 * @param n     Plane normal (normalized).
 * @param d     Plane offset.
 * @param h     Output hit record (filled on success).
 * @param matId Material ID to assign if hit.
 * @return True if the ray hits the plane at t >= uEPS.
 */
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

/**
 * @brief Intersects a ray with a sphere.
 *
 * Sphere equation: |x - c|^2 = r^2
 *
 * @param ro    Ray origin.
 * @param rd    Ray direction (assumed normalized).
 * @param c     Sphere center.
 * @param r     Sphere radius.
 * @param h     Output hit record (filled on success).
 * @param matId Material ID to assign if hit.
 * @return True if the ray hits the sphere at t >= uEPS.
 */
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

/**
 * @brief Core analytic scene tracer with optional inclusion flags.
 *
 * This function traces against:
 *  - Floor (always)
 *  - Left diffuse sphere (always)
 *  - Glass sphere (controlled by includeGlass)
 *  - Mirror sphere (always)
 *  - Point-light marker sphere (controlled by includePointLightSphere & uPointLightEnabled)
 *
 * It returns the closest hit in @p hit if any intersection is found.
 *
 * @param ro                      Ray origin.
 * @param rd                      Ray direction.
 * @param includeGlass            Include the glass sphere in intersection tests.
 * @param includePointLightSphere Include the point-light marker sphere.
 * @param hit                     Output closest hit (valid only if function returns true).
 * @return True if any object was hit; false otherwise.
 */
bool traceAnalyticCore(vec3 ro, vec3 rd, bool includeGlass, bool includePointLightSphere, out Hit hit) {
    hit.t = uINF;
    Hit h;

    // Floor
    if (intersectPlane(ro, rd, kFloorNormal, kFloorD, h, MAT_FLOOR) && h.t < hit.t) {
        hit = h;
    }

    // Left albedo sphere
    if (intersectSphere(ro, rd, kSphereLeftCenter, kSphereLeftRadius, h, MAT_ALBEDO_SPHERE) && h.t < hit.t) {
        hit = h;
    }

    // Glass sphere (optionally ignored)
    if (includeGlass) {
        if (intersectSphere(ro, rd, kGlassCenter, kGlassRadius, h, MAT_GLASS_SPHERE) && h.t < hit.t) {
            hit = h;
        }
    }

    // Mirror sphere
    if (intersectSphere(ro, rd, kMirrorCenter, kMirrorRadius, h, MAT_MIRROR_SPHERE) && h.t < hit.t) {
        hit = h;
    }

    // Point-light marker sphere (optional)
    if (includePointLightSphere && uPointLightEnabled == 1) {
        // Center is the actual point light position
        if (intersectSphere(ro, rd, uPointLightPos, kPointLightRadius, h, MAT_POINTLIGHT_SPHERE) && h.t < hit.t) {
            hit = h;
        }
    }

    return hit.t < uINF;
}

/**
 * @brief Full analytic scene ray trace (floor + all spheres + marker).
 *
 * This is the general-purpose analytic tracing function used for primary
 * rays and most shading paths.
 */
bool traceAnalytic(vec3 ro, vec3 rd, out Hit hit) {
    return traceAnalyticCore(ro, rd, true, true, hit);
}

/**
 * @brief Analytic scene trace that ignores the glass sphere.
 *
 * Useful for refraction/glass paths where we want to see what lies
 * behind the glass without intersecting the glass surface itself.
 */
bool traceAnalyticIgnoreGlass(vec3 ro, vec3 rd, out Hit hit) {
    return traceAnalyticCore(ro, rd, false, true, hit);
}

/**
 * @brief Analytic scene trace that ignores the point-light marker sphere.
 *
 * Used for shadow rays toward the point light so that the emissive
 * marker sphere does not shadow its own light.
 */
bool traceAnalyticIgnorePointLight(vec3 ro, vec3 rd, out Hit hit) {
    return traceAnalyticCore(ro, rd, true, false, hit);
}

// -------- Sky ----------

/**
 * @brief Environment lighting lookup.
 *
 * If uUseEnvMap == 1, samples a cubemap environment and scales it.
 * Otherwise, returns a simple analytic gradient sky:
 *  - lighter near the horizon, darker towards the zenith.
 *
 * @param dir Normalized direction vector.
 * @return Environment radiance in that direction.
 */
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