#pragma once

#include "app/state.h"

/**
 * @class Application
 * @brief Main entry point of the rendering engine.
 *
 * This class owns the global application state and manages the full
 * lifecycle of the program: window creation, OpenGL initialization,
 * resource setup, main loop execution, and shutdown.
 *
 * The implementation is intentionally compact, acting as the glue
 * between platform-specific initialization (GLFW), renderer state,
 * and the per-frame update/render flow.
 */
class Application {
public:
    /**
     * @brief Constructs an empty application instance.
     *
     * No heavy initialization is performed here; the constructor only
     * sets up basic fields. All resource creation is deferred to run()
     * and its helper initialization functions.
     */
    Application();

    /**
     * @brief Releases application resources and shuts down subsystems.
     *
     * The destructor calls shutdown() to guarantee proper release
     * of OpenGL resources, UI state, and the GLFW window if not done
     * earlier. This ensures the application always exits cleanly.
     */
    ~Application();

    /**
     * @brief Starts the application and enters the main loop.
     *
     * This method initializes the window, OpenGL state, internal
     * rendering state, and then continuously renders frames until
     * the user closes the window. The returned integer can be used
     * as the program's exit code.
     *
     * @return 0 on clean exit, non-zero if initialization failed.
     */
    int run();

private:
    /// Global application state containing renderers, UI, and GPU resources.
    AppState app;

    /// GLFW window handle used throughout the program.
    GLFWwindow *window = nullptr;

    /// Whether initialization succeeded and shutdown routines should run.
    bool initialized = false;

    /**
     * @brief Creates the GLFW window and initializes the OpenGL context.
     *
     * Sets up the core windowing environment. Returns false if the window
     * could not be created or OpenGL initialization fails.
     *
     * @return True on success, false otherwise.
     */
    bool initWindow();

    /**
     * @brief Allocates OpenGL resources required before entering the main loop.
     *
     * This includes shader compilation, buffer creation, VAO setup,
     * and any other GPU-side initialization needed for rendering.
     */
    void initGLResources();

    /**
     * @brief Initializes the rendering and UI state.
     *
     * Loads default parameters, resets accumulators, and prepares
     * the AppState so that the first frame can render consistently.
     */
    void initState();

    /**
     * @brief Main frame loop handling input, update, and rendering.
     *
     * Continues until the GLFW window is closed. Each iteration
     * gathers input, updates internal state, and draws the frame.
     */
    void mainLoop();

    /**
     * @brief Safely destroys GPU resources and shuts down the engine.
     *
     * Ensures deterministic cleanup of all OpenGL objects, UI systems,
     * and the GLFW window. Called automatically on destructor, but
     * can also be triggered explicitly.
     */
    void shutdown();
};
