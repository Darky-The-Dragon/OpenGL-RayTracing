#include "render/render.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include "glm/gtc/type_ptr.hpp"

// Compute the point light position in world space,
// optionally orbiting around the base position.
glm::vec3 computePointLightWorldPos(const RenderParams &params) {
    glm::vec3 base(params.pointLightPos[0],
                   params.pointLightPos[1],
                   params.pointLightPos[2]);

    if (!params.pointLightOrbitEnabled || params.pointLightOrbitRadius <= 0.0f) {
        return base;
    }

    float yawRad = glm::radians(params.pointLightYaw);
    float pitchRad = glm::radians(params.pointLightPitch);

    float cy = cosf(yawRad);
    float sy = sinf(yawRad);
    float cp = cosf(pitchRad);
    float sp = sinf(pitchRad);

    glm::vec3 dir;
    dir.x = cp * sy;
    dir.y = sp;
    dir.z = cp * cy;

    return base + dir * params.pointLightOrbitRadius;
}

// Build a normalized direction vector from yaw/pitch angles (in degrees).
// Used to drive sun and sky directions.
static glm::vec3 dirFromYawPitch(float yawDeg, float pitchDeg) {
    float yaw = glm::radians(yawDeg);
    float pitch = glm::radians(pitchDeg);

    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    float cy = std::cos(yaw);
    float sy = std::sin(yaw);

    glm::vec3 d(cp * cy, sp, cp * sy);

    // Fallback if direction degenerates
    if (glm::dot(d, d) < 1e-6f) {
        return {0.0f, -1.0f, 0.0f};
    }
    return glm::normalize(d);
}

// Ray-traced path: write into accumulation + motion + GBuffer,
// then run the present pass (TAA + SVGF) to the default framebuffer.
void renderRay(AppState &app, const int fbw, const int fbh, const bool cameraMoved,
               const glm::mat4 &currView, const glm::mat4 &currProj) {
    glEnable(GL_SCISSOR_TEST);
    app.accum.bindWriteFBO_MRT(app.gBuffer.posTex, app.gBuffer.nrmTex);
    glViewport(0, 0, fbw, fbh);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    Shader &rt = *app.rtShader;
    rt.use();

    // Build camera basis from the view matrix
    const glm::vec3 right = glm::normalize(glm::vec3(currView[0][0], currView[1][0], currView[2][0]));
    const glm::vec3 up = glm::normalize(glm::vec3(currView[0][1], currView[1][1], currView[2][1]));
    const glm::vec3 fwd = -glm::normalize(glm::vec3(currView[0][2], currView[1][2], currView[2][2]));
    const float tanHalfFov = std::tanf(glm::radians(app.camera.Fov) * 0.5f);

    // Camera / primary-ray uniforms
    rt.setVec3("uCamPos", app.camera.Position);
    rt.setVec3("uCamRight", right);
    rt.setVec3("uCamUp", up);
    rt.setVec3("uCamFwd", fwd);
    rt.setFloat("uTanHalfFov", tanHalfFov);
    rt.setFloat("uAspect", app.camera.AspectRatio);
    rt.setInt("uFrameIndex", app.accum.frameIndex);
    rt.setVec2("uResolution", glm::vec2(fbw, fbh));
    rt.setInt("uSpp", app.showMotion ? 1 : app.params.sppPerFrame);

    // --- Material uniforms (analytic scene) ---------------------------------

    // Albedo sphere
    rt.setVec3("uMatAlbedo_AlbedoColor", glm::make_vec3(app.params.matAlbedoColor));
    rt.setFloat("uMatAlbedo_SpecStrength", app.params.matAlbedoSpecStrength);
    rt.setFloat("uMatAlbedo_Gloss", app.params.matAlbedoGloss);

    // Glass sphere
    rt.setInt("uMatGlass_Enabled", app.params.matGlassEnabled);
    rt.setVec3("uMatGlass_Albedo", glm::make_vec3(app.params.matGlassColor));
    rt.setFloat("uMatGlass_IOR", app.params.matGlassIOR);
    rt.setFloat("uMatGlass_Distortion", app.params.matGlassDistortion);

    // Mirror sphere
    rt.setInt("uMatMirror_Enabled", app.params.matMirrorEnabled);
    rt.setVec3("uMatMirror_Albedo", glm::make_vec3(app.params.matMirrorColor));
    rt.setFloat("uMatMirror_Gloss", app.params.matMirrorGloss);

    // Environment map settings
    rt.setInt("uUseEnvMap", (app.params.enableEnvMap && app.envMapTex) ? 1 : 0);
    rt.setFloat("uEnvIntensity", app.params.envMapIntensity);
    rt.setInt("uEnvMap", 5);

    // Jitter (for TAA / stochastic sampling)
    rt.setVec2("uJitter", app.frame.jitter);
    rt.setInt("uEnableJitter", app.params.enableJitter ? 1 : 0);

    // Scene / BVH toggle and stats
    rt.setInt("uUseBVH", app.useBVH ? 1 : 0);
    rt.setInt("uNodeCount", app.bvhNodeCount);
    rt.setInt("uTriCount", app.bvhTriCount);

    // TAA parameters
    rt.setFloat("uTaaStillThresh", app.params.taaStillThresh);
    rt.setFloat("uTaaHardMovingThresh", app.params.taaHardMovingThresh);
    rt.setFloat("uTaaHistoryMinWeight", app.params.taaHistoryMinWeight);
    rt.setFloat("uTaaHistoryAvgWeight", app.params.taaHistoryAvgWeight);
    rt.setFloat("uTaaHistoryMaxWeight", app.params.taaHistoryMaxWeight);
    rt.setFloat("uTaaHistoryBoxSize", app.params.taaHistoryBoxSize);
    rt.setInt("uEnableTAA", app.params.enableTAA);

    // GI / AO parameters
    rt.setFloat("uGiScaleAnalytic", app.params.giScaleAnalytic);
    rt.setFloat("uGiScaleBVH", app.params.giScaleBVH);
    rt.setInt("uEnableGI", app.params.enableGI);
    rt.setInt("uEnableAO", app.params.enableAO);
    rt.setInt("uAO_SAMPLES", app.params.aoSamples);
    rt.setFloat("uAO_RADIUS", app.params.aoRadius);
    rt.setFloat("uAO_BIAS", app.params.aoBias);
    rt.setFloat("uAO_MIN", app.params.aoMin);

    // Motion vector / reprojection state
    rt.setInt("uShowMotion", app.showMotion ? 1 : 0);
    rt.setInt("uCameraMoved", cameraMoved ? 1 : 0);
    rt.setMat4("uPrevViewProj", app.frame.prevViewProj);
    rt.setMat4("uCurrViewProj", app.frame.currViewProj);
    rt.setVec2("uResolution", glm::vec2(fbw, fbh)); // duplicate but harmless

    // Global numeric constants
    rt.setFloat("uEPS", RenderParams::EPS);
    rt.setFloat("uPI", RenderParams::PI);
    rt.setFloat("uINF", RenderParams::INF);

    // --- Hybrid lights: sun / sky / point -----------------------------------

    // Directional sun
    glm::vec3 sunDir = dirFromYawPitch(app.params.sunYaw, app.params.sunPitch);
    rt.setInt("uSunEnabled", app.params.sunEnabled);
    rt.setVec3("uSunColor", glm::make_vec3(app.params.sunColor));
    rt.setFloat("uSunIntensity", app.params.sunIntensity);
    rt.setVec3("uSunDir", sunDir);

    // Sky dome
    glm::vec3 skyDir = dirFromYawPitch(app.params.skyYaw, app.params.skyPitch);
    rt.setInt("uSkyEnabled", app.params.skyEnabled);
    rt.setVec3("uSkyColor", glm::make_vec3(app.params.skyColor));
    rt.setFloat("uSkyIntensity", app.params.skyIntensity);
    rt.setVec3("uSkyUpDir", skyDir);

    // Local point light (+ analytic marker sphere in shaders)
    glm::vec3 pointPos = computePointLightWorldPos(app.params);
    rt.setInt("uPointLightEnabled", app.params.pointLightEnabled);
    rt.setVec3("uPointLightPos", pointPos);
    rt.setVec3("uPointLightColor", glm::make_vec3(app.params.pointLightColor));
    rt.setFloat("uPointLightIntensity", app.params.pointLightIntensity);

    // --- Bind textures / buffers for ray pass --------------------------------

    // History + M2 (TAA input)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.accum.readTex());
    rt.setInt("uPrevAccum", 0);

    // BVH node buffer
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, app.bvh.nodeTex);
    rt.setInt("uBvhNodes", 1);

    // BVH triangle buffer
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, app.bvh.triTex);
    rt.setInt("uBvhTris", 2);

    // Environment cubemap
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, app.envMapTex);
    rt.setInt("uEnvMap", 5);
    rt.setInt("uUseEnvMap", (app.params.enableEnvMap && app.envMapTex) ? 1 : 0);

    // Fullscreen triangle for ray tracing
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ------------------------------------------------------------------------
    // Present pass: TAA + SVGF + tonemapping to the default framebuffer
    // ------------------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbw, fbh);

    const Shader &present = *app.presentShader;
    present.use();

    // TAA input: current accumulation result
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.accum.writeTex());
    present.setInt("uTex", 0);
    present.setFloat("uExposure", app.params.exposure);

    // Motion vectors for debug / SVGF
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.accum.motionTex);
    present.setInt("uMotionTex", 1);
    present.setInt("uShowMotion", app.showMotion ? 1 : 0);
    present.setFloat("uMotionScale", app.params.motionScale);
    present.setVec2("uResolution", glm::vec2(fbw, fbh));

    // GBuffer: position + normal for edge-aware SVGF
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.gBuffer.posTex);
    present.setInt("uGPos", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, app.gBuffer.nrmTex);
    present.setInt("uGNrm", 3);

    // SVGF parameters
    present.setFloat("uVarMax", app.params.svgfVarMax);
    present.setFloat("uKVar", app.params.svgfKVar);
    present.setFloat("uKColor", app.params.svgfKColor);
    present.setFloat("uKVarMotion", app.params.svgfKVarMotion);
    present.setFloat("uKColorMotion", app.params.svgfKColorMotion);
    present.setFloat("uSvgfStrength", app.params.svgfStrength);
    present.setInt("uEnableSVGF", app.params.enableSVGF ? 1 : 0);

    // Fullscreen triangle for present pass
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Advance ping-pong + frame index for accumulation
    app.accum.swapAfterFrame();
}

// Simple raster fallback: draw the models with flat colors.
// Useful for debugging camera and BVH geometry.
void renderRaster(const AppState &app, const int fbw, const int fbh,
                  const glm::mat4 &currView, const glm::mat4 &currProj) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glClearColor(0.1f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Shader &raster = *app.rasterShader;
    raster.use();
    raster.setMat4("view", currView);
    raster.setMat4("projection", currProj);

    // Ground plane
    auto model = glm::mat4(1.0f);
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.1f, 0.4f, 0.1f));
    app.ground->Draw();

    // Bunny mesh
    model = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 1.5f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5f));
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.9f));
    app.bunny->Draw();

    // Sphere mesh
    model = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5f));
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.3f, 0.6f, 1.0f));
    app.sphere->Draw();

    // Point light marker (small emissive sphere in raster mode)
    if (app.params.pointLightEnabled) {
        glm::vec3 pointPos = computePointLightWorldPos(app.params);

        model = glm::translate(glm::mat4(1.0f), pointPos);
        model = glm::scale(model, glm::vec3(0.15f));
        raster.setMat4("model", model);

        glm::vec3 col = glm::make_vec3(app.params.pointLightColor) * 3.0f;
        raster.setVec3("uColor", col);
        app.sphere->Draw();
    }
}
