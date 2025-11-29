#pragma once

#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

class Shader {
public:
    unsigned int ID = 0;
    bool valid = false;

    // Non-copyable, movable RAII wrapper around a GL program
    Shader(const Shader &) = delete;

    Shader &operator=(const Shader &) = delete;

    Shader(Shader &&) noexcept;

    Shader &operator=(Shader &&) noexcept;

    Shader(const char *vertexPath, const char *fragmentPath);

    ~Shader();

    void use() const;

    [[nodiscard]] bool isValid() const { return valid; }

    void setBool(const std::string &name, bool value) const;

    void setInt(const std::string &name, int value) const;

    void setFloat(const std::string &name, float value) const;

    void setMat4(const std::string &name, const glm::mat4 &mat) const;

    void setVec3(const std::string &name, const glm::vec3 &value) const;

    void setVec2(const std::string &name, const glm::vec2 &value) const;

private:
    mutable std::unordered_map<std::string, int> uniformCache;

    int uniformLocation(const std::string &name) const;

    static void checkCompileErrors(unsigned int shader, const std::string &type);
};
