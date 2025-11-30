#include "app/application.h"

// Simple entry point that delegates to the Application class.
// All setup, main loop, and cleanup live inside Application::run().
int main() {
    Application app;
    return app.run();
}
