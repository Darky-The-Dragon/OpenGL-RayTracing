#include "render/render.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include "glm/gtc/type_ptr.hpp"

static glm::vec3 computePointLightWorldPos(const RenderParams &params, int frameIndex) {
    glm::vec3 center(params.pointLightPos[0],
                     params.pointLightPos[1],
                     params.pointLightPos[2]);

    if (!params.pointLightOrbitEnabled) {
        return center;
    }

    float angle = params.pointLightOrbitSpeed * static_cast<float>(frameIndex);
    float radius = params.pointLightOrbitRadius;

    glm::vec3 pos = center;
    pos.x = center.x + std::cos(angle) * radius;
    pos.z = center.z + std::sin(angle) * radius;
    // keep Y the same as base center
    return pos;
}

static glm::vec3 dirFromYawPitch(float yawDeg, float pitchDeg) {
    float yaw   = glm::radians(yawDeg);
    float pitch = glm::radians(pitchDeg);

    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    float cy = std::cos(yaw);
    float sy = std::sin(yaw);

    glm::vec3 d(cp * cy, sp, cp * sy);

    // Fallback if direction degenerates
    if (glm::dot(d, d) < 1e-6f) {
        return glm::vec3(0.0f, -1.0f, 0.0f);
    }
    return glm::normalize(d);
}

void renderRay(AppState &app, const int fbw, const int fbh, const bool cameraMoved,
               const glm::mat4 &currView, const glm::mat4 &currProj) {
    glEnable(GL_SCISSOR_TEST);
    app.accum.bindWriteFBO_MRT(app.gBuffer.posTex, app.gBuffer.nrmTex);
    glViewport(0, 0, fbw, fbh);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    Shader &rt = *app.rtShader;
    rt.use();

    const glm::vec3 right = glm::normalize(glm::vec3(currView[0][0], currView[1][0], currView[2][0]));
    const glm::vec3 up = glm::normalize(glm::vec3(currView[0][1], currView[1][1], currView[2][1]));
    const glm::vec3 fwd = -glm::normalize(glm::vec3(currView[0][2], currView[1][2], currView[2][2]));
    const float tanHalfFov = std::tanf(glm::radians(app.camera.Fov) * 0.5f);

    rt.setVec3("uCamPos", app.camera.Position);
    rt.setVec3("uCamRight", right);
    rt.setVec3("uCamUp", up);
    rt.setVec3("uCamFwd", fwd);
    rt.setFloat("uTanHalfFov", tanHalfFov);
    rt.setFloat("uAspect", app.camera.AspectRatio);
    rt.setInt("uFrameIndex", app.accum.frameIndex);
    rt.setVec2("uResolution", glm::vec2(fbw, fbh));
    rt.setInt("uSpp", app.showMotion ? 1 : app.params.sppPerFrame);

    // --- Albedo ---
    rt.setVec3("uMatAlbedo_AlbedoColor", glm::make_vec3(app.params.matAlbedoColor));
    rt.setFloat("uMatAlbedo_SpecStrength", app.params.matAlbedoSpecStrength);
    rt.setFloat("uMatAlbedo_Gloss", app.params.matAlbedoGloss);

    // --- Glass ---
    rt.setInt("uMatGlass_Enabled", app.params.matGlassEnabled);
    rt.setVec3("uMatGlass_Albedo", glm::make_vec3(app.params.matGlassColor));
    rt.setFloat("uMatGlass_IOR", app.params.matGlassIOR);
    rt.setFloat("uMatGlass_Distortion", app.params.matGlassDistortion);

    // --- Mirror ---
    rt.setInt("uMatMirror_Enabled", app.params.matMirrorEnabled);
    rt.setVec3("uMatMirror_Albedo", glm::make_vec3(app.params.matMirrorColor));
    rt.setFloat("uMatMirror_Gloss", app.params.matMirrorGloss);

    // Env map
    rt.setInt("uUseEnvMap", (app.params.enableEnvMap && app.envMapTex) ? 1 : 0);
    rt.setFloat("uEnvIntensity", app.params.envMapIntensity);
    rt.setInt("uEnvMap", 5);

    // Jitter
    rt.setVec2("uJitter", app.frame.jitter);
    rt.setInt("uEnableJitter", app.params.enableJitter ? 1 : 0);

    // Scene / BVH
    rt.setInt("uUseBVH", app.useBVH ? 1 : 0);
    rt.setInt("uNodeCount", app.bvhNodeCount);
    rt.setInt("uTriCount", app.bvhTriCount);

    // TAA
    rt.setFloat("uTaaStillThresh", app.params.taaStillThresh);
    rt.setFloat("uTaaHardMovingThresh", app.params.taaHardMovingThresh);
    rt.setFloat("uTaaHistoryMinWeight", app.params.taaHistoryMinWeight);
    rt.setFloat("uTaaHistoryAvgWeight", app.params.taaHistoryAvgWeight);
    rt.setFloat("uTaaHistoryMaxWeight", app.params.taaHistoryMaxWeight);
    rt.setFloat("uTaaHistoryBoxSize", app.params.taaHistoryBoxSize);
    rt.setInt("uEnableTAA", app.params.enableTAA);

    // GI / AO
    rt.setFloat("uGiScaleAnalytic", app.params.giScaleAnalytic);
    rt.setFloat("uGiScaleBVH", app.params.giScaleBVH);
    rt.setInt("uEnableGI", app.params.enableGI);
    rt.setInt("uEnableAO", app.params.enableAO);
    rt.setInt("uAO_SAMPLES", app.params.aoSamples);
    rt.setFloat("uAO_RADIUS", app.params.aoRadius);
    rt.setFloat("uAO_BIAS", app.params.aoBias);
    rt.setFloat("uAO_MIN", app.params.aoMin);

    // Motion / reprojection
    rt.setInt("uShowMotion", app.showMotion ? 1 : 0);
    rt.setInt("uCameraMoved", cameraMoved ? 1 : 0);
    rt.setMat4("uPrevViewProj", app.frame.prevViewProj);
    rt.setMat4("uCurrViewProj", app.frame.currViewProj);
    rt.setVec2("uResolution", glm::vec2(fbw, fbh)); // (duplicated, harmless)

    // Constants
    rt.setFloat("uEPS", RenderParams::EPS);
    rt.setFloat("uPI", RenderParams::PI);
    rt.setFloat("uINF", RenderParams::INF);

    // --- Lights: Sun / Sky / Point ---

    // Sun
    glm::vec3 sunDir = dirFromYawPitch(app.params.sunYaw, app.params.sunPitch);
    rt.setInt("uSunEnabled", app.params.sunEnabled);
    rt.setVec3("uSunColor", glm::make_vec3(app.params.sunColor));
    rt.setFloat("uSunIntensity", app.params.sunIntensity);
    rt.setVec3("uSunDir", sunDir);

    // Sky
    glm::vec3 skyDir = dirFromYawPitch(app.params.skyYaw, app.params.skyPitch);
    rt.setInt("uSkyEnabled", app.params.skyEnabled);
    rt.setVec3("uSkyColor", glm::make_vec3(app.params.skyColor));
    rt.setFloat("uSkyIntensity", app.params.skyIntensity);
    rt.setVec3("uSkyUpDir", skyDir);

    // Point
    glm::vec3 pointPos = computePointLightWorldPos(app.params, app.accum.frameIndex);
    rt.setInt("uPointLightEnabled", app.params.pointLightEnabled);
    rt.setVec3("uPointLightPos", pointPos);
    rt.setVec3("uPointLightColor", glm::make_vec3(app.params.pointLightColor));
    rt.setFloat("uPointLightIntensity", app.params.pointLightIntensity);

    // --- Textures / buffers ---

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.accum.readTex());
    rt.setInt("uPrevAccum", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, app.bvh.nodeTex);
    rt.setInt("uBvhNodes", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, app.bvh.triTex);
    rt.setInt("uBvhTris", 2);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, app.envMapTex);
    rt.setInt("uEnvMap", 5);
    rt.setInt("uUseEnvMap", (app.params.enableEnvMap && app.envMapTex) ? 1 : 0);

    // --- Fullscreen tri for ray pass ---
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ------------------------------------------------------------------------
    // Present pass
    // ------------------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbw, fbh);
    const Shader &present = *app.presentShader;
    present.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.accum.writeTex());
    present.setInt("uTex", 0);
    present.setFloat("uExposure", app.params.exposure);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.accum.motionTex);
    present.setInt("uMotionTex", 1);
    present.setInt("uShowMotion", app.showMotion ? 1 : 0);
    present.setFloat("uMotionScale", app.params.motionScale);
    present.setVec2("uResolution", glm::vec2(fbw, fbh));

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.gBuffer.posTex);
    present.setInt("uGPos", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, app.gBuffer.nrmTex);
    present.setInt("uGNrm", 3);

    present.setFloat("uVarMax", app.params.svgfVarMax);
    present.setFloat("uKVar", app.params.svgfKVar);
    present.setFloat("uKColor", app.params.svgfKColor);
    present.setFloat("uKVarMotion", app.params.svgfKVarMotion);
    present.setFloat("uKColorMotion", app.params.svgfKColorMotion);
    present.setFloat("uSvgfStrength", app.params.svgfStrength);
    present.setFloat("uSvgfVarStaticEps", app.params.svgfVarEPS);
    present.setFloat("uSvgfMotionStaticEps", app.params.svgfMotionEPS);
    present.setInt("uEnableSVGF", app.params.enableSVGF ? 1 : 0);

    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    app.accum.swapAfterFrame();
}

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

    // Ground
    auto model = glm::mat4(1.0f);
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.1f, 0.4f, 0.1f));
    app.ground->Draw();

    // Bunny
    model = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 1.5f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5f));
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.9f));
    app.bunny->Draw();

    // Sphere
    model = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5f));
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.3f, 0.6f, 1.0f));
    app.sphere->Draw();

    // Point light marker (small emissive sphere)
    if (app.params.pointLightEnabled) {
        glm::vec3 pointPos = computePointLightWorldPos(app.params, app.accum.frameIndex);

        model = glm::translate(glm::mat4(1.0f), pointPos);
        model = glm::scale(model, glm::vec3(0.15f)); // small sphere
        raster.setMat4("model", model);

        glm::vec3 col = glm::make_vec3(app.params.pointLightColor) * 3.0f;
        raster.setVec3("uColor", col);
        app.sphere->Draw();
    }
}
