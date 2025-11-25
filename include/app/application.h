#pragma once

#include "app/state.h"

class Application {
public:
    Application();
    ~Application();

    int run();

private:
    AppState app;
    GLFWwindow *window = nullptr;

    bool initWindow();
    void initGLResources();
    void initState();
    void mainLoop();
    void shutdown();
};

