#include "ui/gui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdarg>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include "app/paths.h"

namespace ui {
    // ============================================================================
    // Simple in-UI debug console (based on ImGui demo)
    // ============================================================================
    struct DebugConsole {
        ImGuiTextBuffer Buf;
        ImGuiTextFilter Filter;
        ImVector<int> LineOffsets; // Index to lines in Buf
        bool AutoScroll = true;

        DebugConsole() {
            Clear();
        }

        void Clear() {
            Buf.clear();
            LineOffsets.clear();
            LineOffsets.push_back(0);
        }

        void AddLog(const char *fmt, ...) IM_FMTARGS(2) {
            const int old_size = Buf.size();

            va_list args;
            va_start(args, fmt);
            Buf.appendfv(fmt, args);
            va_end(args);

            for (int i = old_size; i < Buf.size(); ++i) {
                if (Buf[i] == '\n')
                    LineOffsets.push_back(i + 1);
            }
        }

        void Draw(const char *title, bool *pOpen) {
            constexpr ImGuiWindowFlags flags =
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing;

            if (!ImGui::Begin(title, pOpen, flags)) {
                ImGui::End();
                return;
            }

            // Top bar: clear + filter
            if (ImGui::Button("Clear"))
                Clear();
            ImGui::SameLine();
            Filter.Draw("Filter");
            ImGui::Separator();

            // Scrolling region
            ImGui::BeginChild(
                "scrolling",
                ImVec2(0, 0),
                false,
                ImGuiWindowFlags_HorizontalScrollbar
            );
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            const char *buf = Buf.begin();
            const char *buf_end = Buf.end();

            if (Filter.IsActive()) {
                // Display only lines that pass filter
                for (int line_no = 0; line_no < LineOffsets.Size; ++line_no) {
                    const char *line_start = buf + LineOffsets[line_no];
                    const char *line_end = (line_no + 1 < LineOffsets.Size)
                                               ? (buf + LineOffsets[line_no + 1] - 1)
                                               : buf_end;
                    if (Filter.PassFilter(line_start, line_end))
                        ImGui::TextUnformatted(line_start, line_end);
                }
            } else {
                // Display everything
                ImGui::TextUnformatted(buf, buf_end);
            }

            ImGui::PopStyleVar();

            if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();
            ImGui::End();
        }
    };

    // ============================================================================
    // Globals
    // ============================================================================
    static DebugConsole gConsole;
    static bool gShowDebugConsole = false;

    // Cached model list for BVH picker
    static std::vector<std::string> gModelFiles;
    static bool gModelScanDone = false;
    static std::string gModelDir = "../models";

    // Cached env-map list for env picker
    static std::vector<std::string> gEnvFiles;
    static bool gEnvScanDone = false;
    static std::string gEnvDir = "../cubemaps";


    // Forward declarations
    static void DrawKeybindLegend();

    static void DrawMainControls(RenderParams &params, const rt::FrameState &frame, const io::InputState &input,
                                 bool &rayMode, bool &useBVH, bool &showMotion);

    // ============================================================================
    // Log: mirror to terminal + GUI console
    // ============================================================================
    void Log(const char *fmt, ...) {
        char buf[1024];

        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // Terminal output
        std::cout << buf;
        std::cout.flush();

        // GUI console output
        gConsole.AddLog("%s", buf);
    }

    // ============================================================================
    // ImGui bootstrap
    // ============================================================================
    void Init(GLFWwindow *window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
        style.FrameRounding = 3.0f;
        style.WindowBorderSize = 0.0f;

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 410");
    }

    void Shutdown() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void BeginFrame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    // ============================================================================
    // Main control panel (top-left, pinned)
    // ============================================================================
    static void DrawMainControls(RenderParams &params, const rt::FrameState &frame, const io::InputState &input,
                                 bool &rayMode, bool &useBVH, bool &showMotion) {
        (void) frame;
        (void) input;

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::Begin("Ray Tracer Controls", nullptr, flags)) {
            ImGui::End();
            return;
        }

        // Metrics
        const ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / io.Framerate);
        ImGui::Separator();

        // ------------------------------------------------------------------------
        // Modes
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool ray = rayMode;
            if (ImGui::Checkbox("Ray Tracing Mode (vs Raster)", &ray)) {
                rayMode = ray;
                Log("[GUI] Ray mode: %s\n", rayMode ? "RAY" : "RASTER");
            }

            bool bvh = useBVH;
            if (ImGui::Checkbox("Use BVH Acceleration", &bvh)) {
                useBVH = bvh;
                Log("[GUI] BVH: %s\n", useBVH ? "ENABLED" : "DISABLED");
            }

            bool motion = showMotion;
            if (ImGui::Checkbox("Show Motion Debug", &motion)) {
                showMotion = motion;
                Log("[GUI] Motion debug: %s\n", showMotion ? "ON" : "OFF");
            }

            bool showDbg = gShowDebugConsole;
            if (ImGui::Checkbox("Show Debug Console", &showDbg)) {
                gShowDebugConsole = showDbg;
                Log("[GUI] Debug console: %s\n", gShowDebugConsole ? "VISIBLE" : "HIDDEN");
            }
        }

        // ------------------------------------------------------------------------
        // Core
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Core", ImGuiTreeNodeFlags_DefaultOpen)) {
            const int oldSpp = params.sppPerFrame;
            if (ImGui::SliderInt("SPP per frame", &params.sppPerFrame, 1, 64)) {
                if (params.sppPerFrame != oldSpp) {
                    Log("[GUI] SPP per frame changed: %d -> %d\n", oldSpp, params.sppPerFrame);
                }
            }

            const float oldExp = params.exposure;
            if (ImGui::SliderFloat("Exposure", &params.exposure, 0.01f, 8.0f, "%.3f")) {
                if (params.exposure != oldExp) {
                    Log("[GUI] Exposure changed: %.4f -> %.4f\n", oldExp, params.exposure);
                }
            }
        }

        // ------------------------------------------------------------------------
        // Environment
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Environment")) {
            bool envEnabled = (params.enableEnvMap != 0);
            if (ImGui::Checkbox("Use Env Map (sky)", &envEnabled)) {
                params.enableEnvMap = envEnabled ? 1 : 0;
                Log("[ENV] Env map: %s\n", envEnabled ? "ENABLED" : "DISABLED");
            }

            const float oldIntensity = params.envMapIntensity;
            if (ImGui::SliderFloat("Env Intensity", &params.envMapIntensity,
                                   0.0f, 5.0f, "%.2f")) {
                if (params.envMapIntensity != oldIntensity) {
                    Log("[ENV] Intensity: %.3f -> %.3f\n",
                        oldIntensity, params.envMapIntensity);
                }
            }

            ImGui::TextWrapped("Select the actual cubemap in the \"Env Map Picker\" window (top-right).");
        }

        // ------------------------------------------------------------------------
        // Jitter
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Jitter")) {
            bool jitter = params.enableJitter;
            if (ImGui::Checkbox("Enable Jitter", &jitter)) {
                params.enableJitter = jitter;
                Log("[GUI] Jitter: %s\n", jitter ? "ENABLED" : "DISABLED");
            }

            ImGui::SeparatorText("Jitter Scales");

            const float oldStill = params.jitterStillScale;
            const float oldMoving = params.jitterMovingScale;

            // Smaller jitter when camera is still
            if (ImGui::SliderFloat("Still Jitter Scale", &params.jitterStillScale, 0.0f, 0.5f, "%.3f")) {
                if (params.jitterStillScale != oldStill) {
                    Log("[GUI] Jitter still scale: %.3f -> %.3f\n",
                        oldStill, params.jitterStillScale);
                }
            }

            // Stronger jitter when the camera is moving
            if (ImGui::SliderFloat("Moving Jitter Scale", &params.jitterMovingScale, 0.0f, 1.0f, "%.3f")) {
                if (params.jitterMovingScale != oldMoving) {
                    Log("[GUI] Jitter moving scale: %.3f -> %.3f\n",
                        oldMoving, params.jitterMovingScale);
                }
            }
        }

        // ------------------------------------------------------------------------
        // Global Illumination
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Global Illumination")) {
            bool gi = params.enableGI;
            if (ImGui::Checkbox("Enable GI", &gi)) {
                params.enableGI = gi;
                Log("[GUI] GI: %s\n", gi ? "ENABLED" : "DISABLED");
            }

            ImGui::SeparatorText("GI Scales");

            const float oldAnalytic = params.giScaleAnalytic;
            if (ImGui::SliderFloat("Analytic GI Scale", &params.giScaleAnalytic, 0.0f, 2.0f)) {
                if (params.giScaleAnalytic != oldAnalytic) {
                    Log("[GUI] Analytic GI scale: %.3f -> %.3f\n",
                        oldAnalytic, params.giScaleAnalytic);
                }
            }

            const float oldBVH = params.giScaleBVH;
            if (ImGui::SliderFloat("BVH GI Scale", &params.giScaleBVH, 0.0f, 2.0f)) {
                if (params.giScaleBVH != oldBVH) {
                    Log("[GUI] BVH GI scale: %.3f -> %.3f\n",
                        oldBVH, params.giScaleBVH);
                }
            }
        }

        // ------------------------------------------------------------------------
        // Ambient Occlusion
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Ambient Occlusion")) {
            bool ao = params.enableAO;
            if (ImGui::Checkbox("Enable AO", &ao)) {
                params.enableAO = ao;
                Log("[GUI] AO: %s\n", ao ? "ENABLED" : "DISABLED");
            }

            ImGui::SeparatorText("AO Parameters");

            const int oldSamples = params.aoSamples;
            if (ImGui::SliderInt("AO Samples", &params.aoSamples, 1, 32)) {
                if (params.aoSamples != oldSamples) {
                    Log("[GUI] AO samples: %d -> %d\n", oldSamples, params.aoSamples);
                }
            }

            const float oldRadius = params.aoRadius;
            if (ImGui::SliderFloat("AO Radius", &params.aoRadius, 0.0f, 4.0f)) {
                if (params.aoRadius != oldRadius) {
                    Log("[GUI] AO radius: %.3f -> %.3f\n",
                        oldRadius, params.aoRadius);
                }
            }

            const float oldBias = params.aoBias;
            if (ImGui::SliderFloat("AO Bias", &params.aoBias, 0.0f, 0.01f, "%.5f")) {
                if (params.aoBias != oldBias) {
                    Log("[GUI] AO bias: %.5f -> %.5f\n",
                        oldBias, params.aoBias);
                }
            }

            const float oldMin = params.aoMin;
            if (ImGui::SliderFloat("AO Min", &params.aoMin, 0.0f, 1.0f)) {
                if (params.aoMin != oldMin) {
                    Log("[GUI] AO min: %.3f -> %.3f\n",
                        oldMin, params.aoMin);
                }
            }
        }

        // ------------------------------------------------------------------------
        // Mirror Reflections
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Mirror Reflections")) {
            bool mir = params.enableMirror;
            if (ImGui::Checkbox("Enable Mirror Bounce", &mir)) {
                params.enableMirror = mir;
                Log("[GUI] Mirror bounce: %s\n", mir ? "ENABLED" : "DISABLED");
            }

            ImGui::SeparatorText("Mirror");

            const float oldMirror = params.mirrorStrength;
            if (ImGui::SliderFloat("Mirror Strength", &params.mirrorStrength, 0.0f, 2.0f)) {
                if (params.mirrorStrength != oldMirror) {
                    Log("[GUI] Mirror strength: %.3f -> %.3f\n",
                        oldMirror, params.mirrorStrength);
                }
            }
        }

        // ------------------------------------------------------------------------
        // TAA
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("TAA")) {
            bool taa = params.enableTAA;
            if (ImGui::Checkbox("Enable TAA", &taa)) {
                params.enableTAA = taa;
                Log("[GUI] TAA: %s\n", taa ? "ENABLED" : "DISABLED");
            }

            const float oldStillThresh = params.taaStillThresh;
            if (ImGui::SliderFloat("Still Threshold", &params.taaStillThresh, 0.0f, 1e-3f, "%.6f")) {
                if (params.taaStillThresh != oldStillThresh) {
                    Log("[GUI] TAA still threshold: %.6f -> %.6f\n",
                        oldStillThresh, params.taaStillThresh);
                }
            }

            const float oldMovingThresh = params.taaHardMovingThresh;
            if (ImGui::SliderFloat("Hard Moving Threshold", &params.taaHardMovingThresh, 0.0f, 1.0f)) {
                if (params.taaHardMovingThresh != oldMovingThresh) {
                    Log("[GUI] TAA moving threshold: %.3f -> %.3f\n",
                        oldMovingThresh, params.taaHardMovingThresh);
                }
            }

            ImGui::SeparatorText("History");

            const float oldMinW = params.taaHistoryMinWeight;
            if (ImGui::SliderFloat("History Min Weight", &params.taaHistoryMinWeight, 0.0f, 1.0f)) {
                if (params.taaHistoryMinWeight != oldMinW) {
                    Log("[GUI] TAA history min weight: %.3f -> %.3f\n",
                        oldMinW, params.taaHistoryMinWeight);
                }
            }

            const float oldAvgW = params.taaHistoryAvgWeight;
            if (ImGui::SliderFloat("History Avg Weight", &params.taaHistoryAvgWeight, 0.0f, 1.0f)) {
                if (params.taaHistoryAvgWeight != oldAvgW) {
                    Log("[GUI] TAA history avg weight: %.3f -> %.3f\n",
                        oldAvgW, params.taaHistoryAvgWeight);
                }
            }

            const float oldMaxW = params.taaHistoryMaxWeight;
            if (ImGui::SliderFloat("History Max Weight", &params.taaHistoryMaxWeight, 0.0f, 1.0f)) {
                if (params.taaHistoryMaxWeight != oldMaxW) {
                    Log("[GUI] TAA history max weight: %.3f -> %.3f\n",
                        oldMaxW, params.taaHistoryMaxWeight);
                }
            }

            const float oldBox = params.taaHistoryBoxSize;
            if (ImGui::SliderFloat("History Box Size", &params.taaHistoryBoxSize, 0.0f, 0.25f)) {
                if (params.taaHistoryBoxSize != oldBox) {
                    Log("[GUI] TAA history box size: %.3f -> %.3f\n",
                        oldBox, params.taaHistoryBoxSize);
                }
            }
        }

        // ------------------------------------------------------------------------
        // SVGF
        // ------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("SVGF Filter")) {
            bool svgf = params.enableSVGF;
            if (ImGui::Checkbox("Enable SVGF", &svgf)) {
                params.enableSVGF = svgf;
                Log("[GUI] SVGF: %s\n", svgf ? "ENABLED" : "DISABLED");
            }

            const float oldStrength = params.svgfStrength;
            if (ImGui::SliderFloat("Strength", &params.svgfStrength, 0.0f, 1.0f)) {
                if (params.svgfStrength != oldStrength) {
                    Log("[GUI] SVGF strength: %.3f -> %.3f\n",
                        oldStrength, params.svgfStrength);
                }
            }

            ImGui::SeparatorText("Variance");

            const float oldVarMax = params.svgfVarMax;
            if (ImGui::SliderFloat("Var Max", &params.svgfVarMax, 0.0f, 0.1f, "%.5f")) {
                if (params.svgfVarMax != oldVarMax) {
                    Log("[GUI] SVGF var max: %.5f -> %.5f\n",
                        oldVarMax, params.svgfVarMax);
                }
            }

            const float oldKVar = params.svgfKVar;
            if (ImGui::SliderFloat("K Var Static", &params.svgfKVar, 0.0f, 500.0f)) {
                if (params.svgfKVar != oldKVar) {
                    Log("[GUI] SVGF K var static: %.3f -> %.3f\n",
                        oldKVar, params.svgfKVar);
                }
            }

            const float oldKColor = params.svgfKColor;
            if (ImGui::SliderFloat("K Color Static", &params.svgfKColor, 0.0f, 100.0f)) {
                if (params.svgfKColor != oldKColor) {
                    Log("[GUI] SVGF K color static: %.3f -> %.3f\n",
                        oldKColor, params.svgfKColor);
                }
            }

            const float oldKVarMov = params.svgfKVarMotion;
            if (ImGui::SliderFloat("K Var Moving", &params.svgfKVarMotion, 0.0f, 500.0f)) {
                if (params.svgfKVarMotion != oldKVarMov) {
                    Log("[GUI] SVGF K var moving: %.3f -> %.3f\n",
                        oldKVarMov, params.svgfKVarMotion);
                }
            }

            const float oldKColorMov = params.svgfKColorMotion;
            if (ImGui::SliderFloat("K Color Moving", &params.svgfKColorMotion, 0.0f, 100.0f)) {
                if (params.svgfKColorMotion != oldKColorMov) {
                    Log("[GUI] SVGF K color moving: %.3f -> %.3f\n",
                        oldKColorMov, params.svgfKColorMotion);
                }
            }

            ImGui::SeparatorText("Epsilons");

            const float oldVarEPS = params.svgfVarEPS;
            if (ImGui::SliderFloat("Var Static Eps", &params.svgfVarEPS, 0.0f, 1e-2f)) {
                if (params.svgfVarEPS != oldVarEPS) {
                    Log("[GUI] SVGF var EPS: %.6f -> %.6f\n",
                        oldVarEPS, params.svgfVarEPS);
                }
            }

            const float oldMotionEPS = params.svgfMotionEPS;
            if (ImGui::SliderFloat("Motion Static Eps", &params.svgfMotionEPS, 0.0f, 0.05f)) {
                if (params.svgfMotionEPS != oldMotionEPS) {
                    Log("[GUI] SVGF motion EPS: %.6f -> %.6f\n",
                        oldMotionEPS, params.svgfMotionEPS);
                }
            }
        }

        ImGui::End();
    }

    // ============================================================================
    // Keybind legend (bottom-right)
    // ============================================================================
    static void DrawKeybindLegend() {
        constexpr float PAD = 10.0f;
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - PAD,
                         vp->WorkPos.y + vp->WorkSize.y - PAD);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));

        constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::Begin("Keybind Legend", nullptr, flags)) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Keybinds / Legend");
        ImGui::Separator();

        ImGui::BeginTable("legend", 2, ImGuiTableFlags_SizingFixedFit);

        auto Row = [&](const char *left, const char *right) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(left);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(right);
        };

        Row("W / A / S / D", "Move camera");
        Row("Mouse", "Look around");
        Row("Scroll", "Change FOV");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Separator();
        ImGui::TableSetColumnIndex(1);
        ImGui::Separator();

        Row("P", "Toggle scene input");
        Row("F2", "Ray / Raster toggle");
        Row("F3 / ↑↓ / 1–4", "Change SPP");
        Row("R", "Reset accumulation");
        Row("F5", "Toggle BVH");
        Row("F6", "Motion debug view");
        Row("[ / ]", "Exposure - / +");
        Row("Esc", "Quit");

        ImGui::EndTable();
        ImGui::End();
    }

    // ============================================================================
    // Public draw entry
    // ============================================================================
    void Draw(RenderParams &params, const rt::FrameState &frame, const io::InputState &input, bool &rayMode,
              bool &useBVH, bool &showMotion, BvhModelPickerState &bvhPicker, EnvMapPickerState &envPicker) {
        // --------------------------------------------------------------
        // Disable ALL ImGui mouse input when scene input (captured mouse) is active.
        // This prevents hovering, clicking, tooltips, highlights, etc.
        // --------------------------------------------------------------
        if (input.sceneInputEnabled) {
            ImGuiIO &io = ImGui::GetIO();

            // Move the mouse far away so nothing is hovered
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

            // Disable mouse buttons so UI cannot click anything
            for (bool &i: io.MouseDown)
                i = false;

            // Optional: ensure wheel scrolling doesn't affect UI
            io.MouseWheel = 0.0f;
            io.MouseWheelH = 0.0f;
        }

        DrawMainControls(params, frame, input, rayMode, useBVH, showMotion);
        DrawKeybindLegend();

        // --------------------------------------------------------------------
        // BVH model picker (top-right) – only visible when BVH is enabled
        // --------------------------------------------------------------------
        if (useBVH) {
            // Scan ../models (or fallback to models) for .obj files once (or when forced)
            if (!gModelScanDone) {
                namespace fs = std::filesystem;
                gModelDir = util::resolve_dir("models");
                gModelFiles.clear();
                try {
                    for (const auto &entry: fs::directory_iterator(gModelDir)) {
                        if (!entry.is_regular_file()) continue;
                        const auto &p = entry.path();
                        if (p.extension() == ".obj") {
                            gModelFiles.push_back(p.string());
                        }
                    }
                } catch (const std::exception &e) {
                    Log("[BVH GUI] Failed to scan '%s': %s\n", gModelDir.c_str(), e.what());
                }

                if (bvhPicker.selectedIndex >= static_cast<int>(gModelFiles.size())) {
                    bvhPicker.selectedIndex = 0;
                }

                if (!gModelFiles.empty()) {
                    std::snprintf(bvhPicker.currentPath,
                                  sizeof(bvhPicker.currentPath),
                                  "%s",
                                  gModelFiles[bvhPicker.selectedIndex].c_str());
                }

                gModelScanDone = true;
            }

            const ImGuiViewport *vp = ImGui::GetMainViewport();
            const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - 10.0f,
                             vp->WorkPos.y + 10.0f);

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

            constexpr ImGuiWindowFlags pickerFlags =
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoCollapse;

            if (ImGui::Begin("BVH Model Picker", nullptr, pickerFlags)) {
                ImGui::Text("Models in %s/", gModelDir.c_str());
                ImGui::Separator();

                if (gModelFiles.empty()) {
                    ImGui::TextUnformatted("No .obj files found.");
                } else {
                    for (int i = 0; i < static_cast<int>(gModelFiles.size()); ++i) {
                        const bool isSelected = (i == bvhPicker.selectedIndex);
                        const char *label = gModelFiles[i].c_str();
                        if (ImGui::Selectable(label, isSelected)) {
                            if (!isSelected) {
                                bvhPicker.selectedIndex = i;
                                std::snprintf(bvhPicker.currentPath,
                                              sizeof(bvhPicker.currentPath),
                                              "%s",
                                              gModelFiles[i].c_str());
                                bvhPicker.reloadRequested = true;
                                Log("[BVH GUI] Selected model: %s\n",
                                    bvhPicker.currentPath);
                            }
                        }
                    }
                }

                if (ImGui::Button("Rescan folder")) {
                    gModelScanDone = false; // trigger rescan next frame
                    Log("[BVH GUI] Rescanning '%s'...\n", gModelDir.c_str());
                }

                ImGui::Separator();
                ImGui::TextWrapped("Current: %s", bvhPicker.currentPath);
            }
            ImGui::End();
        }

                // --------------------------------------------------------------------
        // Env Map picker (top-right, under BVH picker)
        // --------------------------------------------------------------------
        {
            // Scan ./cubemaps (or ../cubemaps) for image files once (or when forced)
            if (!gEnvScanDone) {
                namespace fs = std::filesystem;
                gEnvDir = util::resolve_dir("cubemaps");
                gEnvFiles.clear();
                try {
                    for (const auto &entry : fs::directory_iterator(gEnvDir)) {
                        if (!entry.is_regular_file()) continue;
                        const auto &p = entry.path();
                        auto ext = p.extension().string();
                        if (ext == ".png" || ext == ".jpg" ||
                            ext == ".jpeg" || ext == ".hdr" ||
                            ext == ".exr") {
                            gEnvFiles.push_back(p.string());
                        }
                    }
                } catch (const std::exception &e) {
                    Log("[ENV GUI] Failed to scan '%s': %s\n",
                        gEnvDir.c_str(), e.what());
                }

                if (envPicker.selectedIndex >= static_cast<int>(gEnvFiles.size())) {
                    envPicker.selectedIndex = 0;
                }

                if (!gEnvFiles.empty()) {
                    std::snprintf(envPicker.currentPath,
                                  sizeof(envPicker.currentPath),
                                  "%s",
                                  gEnvFiles[envPicker.selectedIndex].c_str());
                }

                gEnvScanDone = true;
            }

            const ImGuiViewport *vp = ImGui::GetMainViewport();
            // Slightly below the BVH picker (offset y by ~120 px)
            const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - 10.0f,
                             vp->WorkPos.y + 130.0f);

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

            constexpr ImGuiWindowFlags pickerFlags =
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoCollapse;

            if (ImGui::Begin("Env Map Picker", nullptr, pickerFlags)) {
                ImGui::Text("Cubemaps in %s/", gEnvDir.c_str());
                ImGui::Separator();

                if (gEnvFiles.empty()) {
                    ImGui::TextUnformatted("No cubemap images found.");
                } else {
                    for (int i = 0; i < static_cast<int>(gEnvFiles.size()); ++i) {
                        const bool isSelected = (i == envPicker.selectedIndex);
                        const char *label = gEnvFiles[i].c_str();
                        if (ImGui::Selectable(label, isSelected)) {
                            if (!isSelected) {
                                envPicker.selectedIndex = i;
                                std::snprintf(envPicker.currentPath,
                                              sizeof(envPicker.currentPath),
                                              "%s",
                                              gEnvFiles[i].c_str());
                                envPicker.reloadRequested = true;
                                Log("[ENV GUI] Selected env map: %s\n",
                                    envPicker.currentPath);
                            }
                        }
                    }
                }

                if (ImGui::Button("Rescan folder")) {
                    gEnvScanDone = false; // trigger rescan next frame
                    Log("[ENV GUI] Rescanning '%s'...\n", gEnvDir.c_str());
                }

                ImGui::Separator();
                ImGui::TextWrapped("Current: %s", envPicker.currentPath);
            }
            ImGui::End();
        }

        // Big, wide console pinned bottom-left
        if (gShowDebugConsole) {
            const ImGuiViewport *vp = ImGui::GetMainViewport();
            const ImVec2 pos(vp->WorkPos.x + 10.0f,
                             vp->WorkPos.y + vp->WorkSize.y - 10.0f);

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowSize(ImVec2(700.0f, 260.0f), ImGuiCond_Always);
            gConsole.Draw("Debug Console", &gShowDebugConsole);
        }
    }

    void EndFrame() {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
} // namespace ui
