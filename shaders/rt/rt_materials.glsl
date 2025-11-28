// rt_materials.glsl
#ifndef RT_MATERIALS_GLSL
#define RT_MATERIALS_GLSL

// -----------------------------------------------------------------------------
// Shared material / point-light uniforms
// -----------------------------------------------------------------------------
uniform vec3 uMatAlbedo_AlbedoColor;
uniform float uMatAlbedo_SpecStrength;
uniform float uMatAlbedo_Gloss;

uniform vec3 uMatGlass_Albedo;
uniform float uMatGlass_IOR;
uniform float uMatGlass_Distortion;
uniform int uMatGlass_Enabled;

uniform vec3 uMatMirror_Albedo;
uniform float uMatMirror_Gloss;
uniform int uMatMirror_Enabled;

// Material IDs (must match analytic scene)
const int MAT_FLOOR = 0;
const int MAT_ALBEDO_SPHERE = 1;
const int MAT_GLASS_SPHERE = 2;
const int MAT_MIRROR_SPHERE = 3;
const int MAT_POINTLIGHT_SPHERE = 4;

struct MaterialProps {
    vec3 albedo;
    float specStrength;
    float gloss;
    int type;   // 0 = lambert, 1 = mirror, 2 = glass
    float ior;    // index of refraction for glass
};

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

    // Fallback
    m.albedo = vec3(0.8);
    m.specStrength = 0.2;
    m.gloss = 16.0;
    m.type = 0;
    m.ior = 1.0;
    return m;
}

vec3 materialAlbedo(int id) {
    return getMaterial(id).albedo;
}

#endif // RT_MATERIALS_GLSL