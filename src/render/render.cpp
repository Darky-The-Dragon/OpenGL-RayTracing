#include "render/render.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

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

    rt.setInt("uUseEnvMap", app.params.enableEnvMap && app.envMapTex ? 1 : 0);
    rt.setFloat("uEnvIntensity", app.params.envMapIntensity);
    rt.setInt("uEnvMap", 5);

    rt.setVec2("uJitter", app.frame.jitter);
    rt.setInt("uEnableJitter", app.params.enableJitter ? 1 : 0);

    rt.setInt("uUseBVH", app.useBVH ? 1 : 0);
    rt.setInt("uNodeCount", app.bvhNodeCount);
    rt.setInt("uTriCount", app.bvhTriCount);

    rt.setFloat("uTaaStillThresh", app.params.taaStillThresh);
    rt.setFloat("uTaaHardMovingThresh", app.params.taaHardMovingThresh);
    rt.setFloat("uTaaHistoryMinWeight", app.params.taaHistoryMinWeight);
    rt.setFloat("uTaaHistoryAvgWeight", app.params.taaHistoryAvgWeight);
    rt.setFloat("uTaaHistoryMaxWeight", app.params.taaHistoryMaxWeight);
    rt.setFloat("uTaaHistoryBoxSize", app.params.taaHistoryBoxSize);
    rt.setInt("uEnableTAA", app.params.enableTAA);

    rt.setFloat("uGiScaleAnalytic", app.params.giScaleAnalytic);
    rt.setFloat("uGiScaleBVH", app.params.giScaleBVH);
    rt.setInt("uEnableGI", app.params.enableGI);
    rt.setInt("uEnableAO", app.params.enableAO);
    rt.setInt("uEnableMirror", app.params.enableMirror);
    rt.setFloat("uMirrorStrength", app.params.mirrorStrength);
    rt.setInt("uAO_SAMPLES", app.params.aoSamples);
    rt.setFloat("uAO_RADIUS", app.params.aoRadius);
    rt.setFloat("uAO_BIAS", app.params.aoBias);
    rt.setFloat("uAO_MIN", app.params.aoMin);

    rt.setInt("uShowMotion", app.showMotion ? 1 : 0);
    rt.setInt("uCameraMoved", cameraMoved ? 1 : 0);
    rt.setMat4("uPrevViewProj", app.frame.prevViewProj);
    rt.setMat4("uCurrViewProj", app.frame.currViewProj);
    rt.setVec2("uResolution", glm::vec2(fbw, fbh));

    rt.setFloat("uEPS", RenderParams::EPS);
    rt.setFloat("uPI", RenderParams::PI);
    rt.setFloat("uINF", RenderParams::INF);

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

    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

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

void renderRaster(const AppState &app, const int fbw, const int fbh, const glm::mat4 &currView, const glm::mat4 &currProj) {
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

    auto model = glm::mat4(1.0f);
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.1f, 0.4f, 0.1f));
    app.ground->Draw();

    model = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 1.5f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5f));
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.9f));
    app.bunny->Draw();

    model = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5f));
    raster.setMat4("model", model);
    raster.setVec3("uColor", glm::vec3(0.3f, 0.6f, 1.0f));
    app.sphere->Draw();
}
