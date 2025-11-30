#pragma once

#include <filesystem>
#include <string>

/// Small utility helpers (paths, filesystem helpers, misc convenience functions).
namespace util {
    /**
     * @brief Resolves a file path relative to the project root or build directory.
     *
     * When running the application from the build folder, files are usually placed
     * one directory above (e.g., shaders, textures, models). This helper attempts
     * to load resources using the ../ prefix first, and falls back to the provided
     * relative path if the preferred location does not exist.
     *
     * This function simplifies resource loading and avoids hard-coding absolute paths.
     *
     * @param relativePath Path of the resource relative to the project root.
     * @return A resolved path that exists in the filesystem, or the fallback path
     *         if neither location is present.
     */
    inline std::string resolve_path(const std::string &relativePath) {
        namespace fs = std::filesystem;
        const fs::path preferred = fs::path("..") / relativePath;
        if (fs::exists(preferred)) return preferred.string();
        const auto fallback = fs::path(relativePath);
        return fallback.string();
    }

    /**
     * @brief Resolves a directory path relative to the project root or build directory.
     *
     * Similar to resolve_path(), but tailored for directories. The function checks the
     * existence of ../dir first, then dir, and finally returns the best candidate even
     * if neither exists. This is useful for locating folders such as shader directories,
     * model directories, or output directories while keeping the project layout flexible.
     *
     * @param dir Directory path relative to the project root.
     * @return A best-guess resolved directory path, preferring ../dir when available.
     */
    inline std::string resolve_dir(const std::string &dir) {
        namespace fs = std::filesystem;
        const fs::path preferred = fs::path("..") / dir;
        if (fs::exists(preferred)) return preferred.string();
        const auto fallback = fs::path(dir);
        if (fs::exists(fallback)) return fallback.string();
        return fallback.string();
    }
} // namespace util
