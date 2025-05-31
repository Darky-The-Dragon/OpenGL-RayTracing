/*
    Mesh Class

    This class manages the GPU representation of a mesh using modern OpenGL.
    It handles:

    - VAO (Vertex Array Object): remembers how vertex data is laid out in memory.
    - VBO (Vertex Buffer Object): stores vertex attributes like positions and normals.
    - EBO (Element Buffer Object): stores indices that define faces via shared vertices.

    Once initialized, this class allows the mesh to be rendered via a simple .Draw() call.

    References:
    - https://learnopengl.com/Getting-started/Hello-Triangle
    - RAII Principles: https://en.cppreference.com/w/cpp/language/raii
    - Based on: https://github.com/JoeyDeVries/LearnOpenGL/blob/master/includes/learnopengl/mesh.h

    Design Notes:
    - Mesh is a **move-only** class. Copying is disabled to avoid multiple owners of GPU resources.
    - It follows RAII: resources (VBO, VAO, EBO) are acquired and released automatically.
    - No texturing is implemented in this version.
*/

#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/gl.h>

using namespace std;

// Structure describing a single vertex and its attributes
struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
};

class Mesh {
public:
    // Geometry data
    vector<Vertex> vertices;
    vector<GLuint> indices;

    // OpenGL identifiers
    GLuint VAO;

    // Disable copy constructor and copy assignment (move-only)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Constructor (takes ownership of vertex/index data)
    Mesh(vector<Vertex>& vertices, vector<GLuint>& indices) noexcept
        : vertices(std::move(vertices)), indices(std::move(indices)) {
        setupMesh();
    }

    // Move constructor
    Mesh(Mesh&& move) noexcept
        : vertices(std::move(move.vertices)),
          indices(std::move(move.indices)),
          VAO(move.VAO), VBO(move.VBO), EBO(move.EBO) {
        move.VAO = 0; // We use VAO=0 to indicate "moved-from" state
    }

    // Move assignment
    Mesh& operator=(Mesh&& move) noexcept {
        freeGPUresources();
        if (move.VAO) {
            vertices = std::move(move.vertices);
            indices = std::move(move.indices);
            VAO = move.VAO;
            VBO = move.VBO;
            EBO = move.EBO;
            move.VAO = 0;
        } else {
            VAO = 0;
        }
        return *this;
    }

    // Destructor (frees GPU buffers)
    ~Mesh() noexcept {
        freeGPUresources();
    }

    // Render the mesh
    void Draw() {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

private:
    GLuint VBO, EBO;

    // Initializes VAO, VBO, and EBO and configures vertex attribute pointers
    void setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

        // Position attribute (layout = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Position));

        // Normal attribute (layout = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

        // Texture coordinate attribute (layout = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

        // Tangent (layout = 3)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));

        // Bitangent (layout = 4)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0); // EBO remains bound to VAO
    }

    // Deletes VAO, VBO, and EBO if owned
    void freeGPUresources() {
        if (VAO) {
            glDeleteVertexArrays(1, &VAO);
            glDeleteBuffers(1, &VBO);
            glDeleteBuffers(1, &EBO);
        }
    }
};
