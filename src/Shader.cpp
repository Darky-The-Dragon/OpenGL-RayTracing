#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "glad/gl.h"
#include <glm/gtc/type_ptr.hpp>

namespace {
    // Helper: get directory from path, including trailing slash (if any), else empty string
    std::string getDirectory(const std::string &path) {
        const size_t pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            return path.substr(0, pos + 1);
        }
        return "";
    }

    // Helper: preprocess GLSL #include "..." recursively
    std::string preprocessShaderSource(const std::string &source, const std::string &baseDir, int depth = 0) {
        if (depth > 16) {
            std::cerr << "[WARNING] Shader include depth > 16, possible include cycle. "
                    << "Aborting further includes.\n";
            return source;
        }
        std::istringstream input(source);
        std::ostringstream output;
        std::string line;
        while (std::getline(input, line)) {
            // Trim leading whitespace
            size_t first_non_ws = line.find_first_not_of(" \t");
            std::string trimmed = (first_non_ws == std::string::npos) ? "" : line.substr(first_non_ws);
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
                        std::cerr << "ERROR: Could not open included shader file: \"" << incPath << "\" (full path: " <<
                                fullPath << ")" << std::endl;
                        output << line << '\n';
                        continue;
                    }
                    std::stringstream incStream;
                    incStream << incFile.rdbuf();
                    std::string incSrc = incStream.str();
                    output << "// --- begin include: " << incPath << " ---\n";
                    output << preprocessShaderSource(incSrc, getDirectory(fullPath), depth + 1);
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
} // namespace

Shader::Shader(const char *vertexPath, const char *fragmentPath) {
    std::ifstream vFile(vertexPath);
    std::ifstream fFile(fragmentPath);

    if (!vFile || !fFile) {
        std::cerr << "ERROR: Could not open shader files:\n"
                << "Vertex: " << vertexPath << "\n"
                << "Fragment: " << fragmentPath << std::endl;
        ID = 0; // invalid program; use() will bind 0
        return;
    }

    std::stringstream vStream, fStream;
    vStream << vFile.rdbuf();
    fStream << fFile.rdbuf();

    std::string vRaw = vStream.str();
    std::string fRaw = fStream.str();

    // Preprocess GLSL-style #include "..." directives using the directory of each shader
    std::string vCode = preprocessShaderSource(vRaw, getDirectory(vertexPath));
    std::string fCode = preprocessShaderSource(fRaw, getDirectory(fragmentPath));

    std::cout << "[DEBUG] Vertex Shader Length (preprocessed): " << vCode.size() << std::endl;
    std::cout << "[DEBUG] Fragment Shader Length (preprocessed): " << fCode.size() << std::endl;

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

    // Link shaders into program
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

void Shader::use() const {
    glUseProgram(ID);
}

void Shader::setBool(const std::string &name, const bool value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), static_cast<int>(value));
}

void Shader::setInt(const std::string &name, const int value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setFloat(const std::string &name, const float value) const {
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setMat4(const std::string &name, const glm::mat4 &mat) const {
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setVec3(const std::string &name, const glm::vec3 &value) const {
    glUniform3f(glGetUniformLocation(ID, name.c_str()), value.x, value.y, value.z);
}

void Shader::setVec2(const std::string &name, const glm::vec2 &value) const {
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void Shader::checkCompileErrors(const unsigned int shader, const std::string &type) {
    int success = 0;
    char infoLog[1024];

    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << type << " SHADER COMPILATION ERROR:\n" << infoLog << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "SHADER LINKING ERROR:\n" << infoLog << std::endl;
        }
    }
}
