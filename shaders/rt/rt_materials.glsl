// rt_materials.glsl
#ifndef RT_MATERIALS_GLSL
#define RT_MATERIALS_GLSL

// Simple material model for analytic scene:
//  - albedo: base color
//  - specStrength: how strong the specular highlight is (0 = none)
//  - gloss: Phong exponent (higher = sharper highlight)
//  - type: 0 = diffuse/specular, 1 = perfect mirror (handled separately)
struct MaterialProps {
    vec3 albedo;
    float specStrength;
    float gloss;
    int type; // 0 = normal surface, 1 = mirror
};

MaterialProps getMaterial(int id) {
    MaterialProps m;

    // Floor (id = 0)
    if (id == 0) {
        m.albedo = vec3(0.7, 0.7, 0.75);
        m.specStrength = 0.15;
        m.gloss = 16.0;
        m.type = 0;
        return m;
    }

    // Left sphere: red glossy (id = 1)
    if (id == 1) {
        m.albedo = vec3(0.85, 0.25, 0.25);
        m.specStrength = 0.35;
        m.gloss = 48.0;
        m.type = 0;
        return m;
    }

    // (unused) green-ish mat (id = 2)
    if (id == 2) {
        m.albedo = vec3(0.25, 0.85, 0.35);
        m.specStrength = 0.25;
        m.gloss = 24.0;
        m.type = 0;
        return m;
    }

    // Right sphere: perfect mirror (id = 3)
    if (id == 3) {
        m.albedo = vec3(0.95);
        m.specStrength = 0.0;   // no local spec – uses mirror bounce instead
        m.gloss = 1.0;
        m.type = 1;
        return m;
    }

    // Fallback generic grey
    m.albedo = vec3(0.8);
    m.specStrength = 0.2;
    m.gloss = 16.0;
    m.type = 0;
    return m;
}

// Kept for compatibility (if used anywhere else)
vec3 materialAlbedo(int id) {
    return getMaterial(id).albedo;
}

#endif // RT_MATERIALS_GLSL