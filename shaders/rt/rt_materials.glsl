// rt_materials.glsl
#ifndef RT_MATERIALS_GLSL
#define RT_MATERIALS_GLSL

/*
    rt_materials.glsl – Material IDs and Parameter Fetch

    This module defines:
    - Symbolic material IDs for the analytic scene (floor + spheres).
    - A compact MaterialProps struct used during shading.
    - A getMaterial(id) function that maps a material ID to concrete
      parameters, many of which are driven by GUI-controlled uniforms.
    - A convenience helper materialAlbedo(id) for quick albedo queries.

    The IDs here must stay in sync with the analytic scene geometry setup
    on the CPU side (rt_scene_analytic.glsl + C++ scene layout).
*/

// Material IDs (must match analytic scene)
const int MAT_FLOOR = 0;
const int MAT_ALBEDO_SPHERE = 1;
const int MAT_GLASS_SPHERE = 2;
const int MAT_MIRROR_SPHERE = 3;
const int MAT_POINTLIGHT_SPHERE = 4;

/**
 * @brief Material parameter block used by shading code.
 *
 * Fields:
 *  - albedo        : base color (diffuse / tint)
 *  - specStrength  : specular term weight (Phong/other)
 *  - gloss         : specular exponent or glossiness factor
 *  - type          : 0 = lambert/Phong, 1 = mirror, 2 = glass
 *  - ior           : index of refraction for glass materials
 */
struct MaterialProps {
    vec3 albedo;
    float specStrength;
    float gloss;
    int type;   // 0 = lambert, 1 = mirror, 2 = glass
    float ior;  // index of refraction for glass
};

/**
 * @brief Returns material properties for a given material ID.
 *
 * The mapping is:
 *  - MAT_FLOOR           : static neutral grey floor
 *  - MAT_ALBEDO_SPHERE   : GUI-driven base sphere (albedo/spec/gloss)
 *  - MAT_GLASS_SPHERE    : glass sphere (tint + IOR), can be disabled → diffuse
 *  - MAT_MIRROR_SPHERE   : mirror sphere, can be disabled → diffuse fallback
 *  - MAT_POINTLIGHT_SPHERE: emissive sphere – typically handled specially
 *
 * Many parameters are controlled via uniforms so that the GUI can tweak
 * them in real time.
 */
MaterialProps getMaterial(int id) {
    MaterialProps m;

    // Floor – fixed neutral grey
    if (id == MAT_FLOOR) {
        m.albedo = vec3(0.7);
        m.specStrength = 0.1;
        m.gloss = 16.0;
        m.type = 0;
        m.ior = 1.0;
        return m;
    }

    // Left sphere – GUI albedo
    if (id == MAT_ALBEDO_SPHERE) {
        m.albedo = uMatAlbedo_AlbedoColor;
        m.specStrength = uMatAlbedo_SpecStrength;
        m.gloss = uMatAlbedo_Gloss;
        m.type = 0;
        m.ior = 1.0;
        return m;
    }

    // Glass sphere
    if (id == MAT_GLASS_SPHERE) {
        if (uMatGlass_Enabled == 0) {
            // behave like diffuse if disabled
            m.albedo = uMatAlbedo_AlbedoColor;
            m.specStrength = uMatAlbedo_SpecStrength;
            m.gloss = uMatAlbedo_Gloss;
            m.type = 0;
            m.ior = 1.0;
        } else {
            m.albedo = uMatGlass_Albedo;
            m.specStrength = uMatGlass_Distortion; // used by shadeGlass as distortion strength
            m.gloss = 1.0;
            m.type = 2;
            m.ior = uMatGlass_IOR;
        }
        return m;
    }

    // Mirror sphere
    if (id == MAT_MIRROR_SPHERE) {
        if (uMatMirror_Enabled == 0) {
            // fallback to diffuse if disabled
            m.albedo = uMatAlbedo_AlbedoColor;
            m.specStrength = uMatAlbedo_SpecStrength;
            m.gloss = uMatAlbedo_Gloss;
            m.type = 0;
            m.ior = 1.0;
        } else {
            m.albedo = uMatMirror_Albedo;
            m.specStrength = 0.0;
            m.gloss = uMatMirror_Gloss;
            m.type = 1;     // mirror
            m.ior = 1.0;
        }
        return m;
    }

    // Fallback (e.g. pointlight sphere or unknown ID)
    m.albedo = vec3(0.8);
    m.specStrength = 0.2;
    m.gloss = 16.0;
    m.type = 0;
    m.ior = 1.0;
    return m;
}

/**
 * @brief Convenience helper returning only the albedo of a material.
 */
vec3 materialAlbedo(int id) {
    return getMaterial(id).albedo;
}

#endif // RT_MATERIALS_GLSL