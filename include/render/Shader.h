#pragma once

#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

/**
 * @class Shader
 * @brief Minimal RAII wrapper for an OpenGL shader program.
 *
 * This class handles compilation, linking, binding, and uniform management
 * for GLSL vertex/fragment programs. It is intentionally lightweight but
 * provides convenience helpers for setting common uniform types.
 *
 * The class is:
 *  - non-copyable (avoids double deletion of GL programs)
 *  - movable (safe transfer of ownership)
 *
 * The constructor takes file paths and builds a complete shader program.
 */
class Shader {
public:
    /// OpenGL program object ID.
    unsigned int ID = 0;

    /// Whether the shader compiled and linked successfully.
    bool valid = false;

    // -------------------------------------------------------------------------
    // Rule-of-Five: non-copyable but movable RAII wrapper
    // -------------------------------------------------------------------------

    /// Deleted copy constructor to prevent accidental copying.
    Shader(const Shader &) = delete;

    /// Deleted copy assignment.
    Shader &operator=(const Shader &) = delete;

    /**
     * @brief Move constructor — transfers program ownership.
     *
     * After the move, the source object is left in a safe, invalid state.
     */
    Shader(Shader &&) noexcept;

    /**
     * @brief Move assignment — releases current program and adopts another.
     */
    Shader &operator=(Shader &&) noexcept;

    /**
     * @brief Constructs and links a GLSL program from two file paths.
     *
     * @param vertexPath   Path to the vertex shader file.
     * @param fragmentPath Path to the fragment shader file.
     *
     * The constructor loads, compiles, links, and validates the program.
     */
    Shader(const char *vertexPath, const char *fragmentPath);

    /**
     * @brief Destructor releases the GL program if valid.
     */
    ~Shader();

    /**
     * @brief Binds the shader program for subsequent draw calls.
     */
    void use() const;

    /**
     * @return True if the shader successfully compiled and linked.
     */
    [[nodiscard]] bool isValid() const { return valid; }

    // -------------------------------------------------------------------------
    // Uniform setters
    // -------------------------------------------------------------------------

    /**
     * @brief Sets a boolean uniform.
     * @param name Uniform name as in the GLSL source.
     * @param value Boolean value to upload.
     */
    void setBool(const std::string &name, bool value) const;

    /**
     * @brief Sets an integer uniform.
     */
    void setInt(const std::string &name, int value) const;

    /**
     * @brief Sets a float uniform.
     */
    void setFloat(const std::string &name, float value) const;

    /**
     * @brief Sets a mat4 uniform (typically view/projection matrices).
     */
    void setMat4(const std::string &name, const glm::mat4 &mat) const;

    /**
     * @brief Sets a vec3 uniform.
     */
    void setVec3(const std::string &name, const glm::vec3 &value) const;

    /**
     * @brief Sets a vec2 uniform.
     */
    void setVec2(const std::string &name, const glm::vec2 &value) const;

private:
    // -------------------------------------------------------------------------
    // Internal utilities
    // -------------------------------------------------------------------------

    /**
     * @brief Cache to avoid repeated uniform location lookups.
     *
     * The cache is mutable so it can be updated even when the Shader
     * instance is referenced as const (uniform setters are const).
     */
    mutable std::unordered_map<std::string, int> uniformCache;

    /**
     * @brief Retrieves (and caches) the location of a uniform variable.
     *
     * @param name Name of the uniform variable in the shader.
     * @return The GL uniform location, or -1 if not found.
     */
    int uniformLocation(const std::string &name) const;

    /**
     * @brief Checks shader compilation and linking errors.
     *
     * Prints detailed messages to the console on failure.
     *
     * @param shader Shader or program ID.
     * @param type   Type of shader stage ("VERTEX", "FRAGMENT", "PROGRAM").
     */
    static void checkCompileErrors(unsigned int shader, const std::string &type);
};
