#include "render/Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "glad/gl.h"
#include <glm/gtc/type_ptr.hpp>

/// Internal shader utilities: GLSL preprocessing, include expansion, and file helpers.
namespace shader_detail {
    // Extract directory part from a path, keeping the trailing slash.
    // Returns empty string if there's no directory.
    std::string getDirectory(const std::string &path) {
        const size_t pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            return path.substr(0, pos + 1);
        }
        return "";
    }

    // Preprocess GLSL #include "file.glsl" recursively.
    // Includes are resolved relative to baseDir.
    std::string preprocessShaderSource(const std::string &source,
                                       const std::string &baseDir,
                                       int depth = 0) {
        if (depth > 16) {
            std::cerr << "[WARNING] Shader include depth > 16, possible include cycle. "
                    << "Aborting further includes.\n";
            return source;
        }

        std::istringstream input(source);
        std::ostringstream output;
        std::string line;

        while (std::getline(input, line)) {
            // Trim leading whitespace to detect "#include" even if indented
            size_t first_non_ws = line.find_first_not_of(" \t");
            std::string trimmed = (first_non_ws == std::string::npos)
                                      ? ""
                                      : line.substr(first_non_ws);

            // Handle #include "..."
            if (trimmed.rfind("#include", 0) == 0) {
                size_t q1 = trimmed.find('"');
                size_t q2 = (q1 != std::string::npos)
                                ? trimmed.find('"', q1 + 1)
                                : std::string::npos;

                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                    std::string incPath = trimmed.substr(q1 + 1, q2 - q1 - 1);
                    std::string fullPath = baseDir + incPath;
                    std::ifstream incFile(fullPath);
                    if (!incFile) {
                        std::cerr << "ERROR: Could not open included shader file: \""
                                << incPath << "\" (full path: " << fullPath << ")\n";
                        // Keep original line so the shader is still somewhat debuggable.
                        output << line << '\n';
                        continue;
                    }

                    std::stringstream incStream;
                    incStream << incFile.rdbuf();
                    std::string incSrc = incStream.str();

                    output << "// --- begin include: " << incPath << " ---\n";
                    output << preprocessShaderSource(
                        incSrc,
                        getDirectory(fullPath),
                        depth + 1
                    );
                    output << "// --- end include: " << incPath << " ---\n";
                } else {
                    // Malformed include, just emit as-is
                    output << line << '\n';
                }
            } else {
                output << line << '\n';
            }
        }
        return output.str();
    }
} // namespace shader_detail

// Move constructor: steal GL program handle from other.
Shader::Shader(Shader &&o) noexcept {
    *this = std::move(o);
}

// Move assignment: delete current program (if any), then steal other's.
Shader &Shader::operator=(Shader &&o) noexcept {
    if (this == &o) return *this;

    if (ID) {
        glDeleteProgram(ID);
    }

    ID = o.ID;
    o.ID = 0;

    // Uniform locations are program-specific, so clear cache here.
    uniformCache.clear();
    return *this;
}

// Construct shader from vertex + fragment paths and build the GL program.
Shader::Shader(const char *vertexPath, const char *fragmentPath) {
    std::ifstream vFile(vertexPath);
    std::ifstream fFile(fragmentPath);

    if (!vFile || !fFile) {
        std::cerr << "ERROR: Could not open shader files:\n"
                << "Vertex: " << vertexPath << "\n"
                << "Fragment: " << fragmentPath << "\n";
        ID = 0;
        valid = false;
        return;
    }

    std::stringstream vStream, fStream;
    vStream << vFile.rdbuf();
    fStream << fFile.rdbuf();

    std::string vRaw = vStream.str();
    std::string fRaw = fStream.str();

    // Expand #include "..." directives relative to each shader's directory.
    std::string vCode = shader_detail::preprocessShaderSource(vRaw, shader_detail::getDirectory(vertexPath));
    std::string fCode = shader_detail::preprocessShaderSource(fRaw, shader_detail::getDirectory(fragmentPath));

    const char *vShaderCode = vCode.c_str();
    const char *fShaderCode = fCode.c_str();

    // Compile vertex shader
    unsigned int vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    // Compile fragment shader
    unsigned int fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    // Link shaders into a single program
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linkStatus = 0;
    glGetProgramiv(ID, GL_LINK_STATUS, &linkStatus);
    valid = (linkStatus == GL_TRUE);
}

// Destroy GL program on shutdown.
Shader::~Shader() {
    if (ID) {
        glDeleteProgram(ID);
        ID = 0;
    }
}

// Bind this shader program if it was successfully created.
void Shader::use() const {
    if (valid) {
        glUseProgram(ID);
    }
}

// Uniform helpers ------------------------------------------------------------

// Bool → GLint uniform.
void Shader::setBool(const std::string &name, const bool value) const {
    glUniform1i(uniformLocation(name), static_cast<int>(value));
}

void Shader::setInt(const std::string &name, const int value) const {
    glUniform1i(uniformLocation(name), value);
}

void Shader::setFloat(const std::string &name, const float value) const {
    glUniform1f(uniformLocation(name), value);
}

void Shader::setMat4(const std::string &name, const glm::mat4 &mat) const {
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setVec3(const std::string &name, const glm::vec3 &value) const {
    glUniform3f(uniformLocation(name), value.x, value.y, value.z);
}

void Shader::setVec2(const std::string &name, const glm::vec2 &value) const {
    glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
}

// Cached uniform location lookup. Returns -1 if program is invalid.
int Shader::uniformLocation(const std::string &name) const {
    if (ID == 0) return -1;

    auto it = uniformCache.find(name);
    if (it != uniformCache.end()) {
        return it->second;
    }

    const int loc = glGetUniformLocation(ID, name.c_str());
    uniformCache.emplace(name, loc);
    return loc;
}

// Print compile / link errors to stderr if something goes wrong.
void Shader::checkCompileErrors(const unsigned int shader, const std::string &type) {
    int success = 0;
    char infoLog[1024];

    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << type << " SHADER COMPILATION ERROR:\n"
                    << infoLog << "\n";
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "SHADER LINKING ERROR:\n"
                    << infoLog << "\n";
        }
    }
}
