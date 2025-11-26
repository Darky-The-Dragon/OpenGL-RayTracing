#pragma once

#include <filesystem>
#include <string>

namespace util {
    // Prefer ../relativePath when running from the build folder; fall back to relativePath in-source.
    inline std::string resolve_path(const std::string &relativePath) {
        namespace fs = std::filesystem;
        fs::path preferred = fs::path("..") / relativePath;
        if (fs::exists(preferred)) return preferred.string();
        fs::path fallback = fs::path(relativePath);
        return fallback.string();
    }

    // Prefer ../dir when it exists; otherwise use dir if it exists. Returns the best guess even if missing.
    inline std::string resolve_dir(const std::string &dir) {
        namespace fs = std::filesystem;
        fs::path preferred = fs::path("..") / dir;
        if (fs::exists(preferred)) return preferred.string();
        fs::path fallback = fs::path(dir);
        if (fs::exists(fallback)) return fallback.string();
        return fallback.string();
    }
} // namespace util