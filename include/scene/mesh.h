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

/**
 * @struct Vertex
 * @brief CPU-side representation of a single vertex.
 *
 * This structure stores the attributes required for most real-time rendering:
 *  - Position (vec3)
 *  - Normal (vec3)
 *  - UV coordinates (vec2)
 *  - Tangent / Bitangent (vec3 each), used for normal mapping
 *
 * Only a subset is used in this project, but the layout matches the common
 * LearnOpenGL-style mesh format.
 */
struct Vertex {
    glm::vec3 Position; ///< Object-space vertex position.
    glm::vec3 Normal; ///< Vertex surface normal.
    glm::vec2 TexCoords; ///< Texture coordinates.
    glm::vec3 Tangent; ///< Tangent vector for TBN basis.
    glm::vec3 Bitangent; ///< Bitangent vector for TBN basis.
};

/**
 * @class Mesh
 * @brief RAII wrapper around an indexed triangle mesh stored on the GPU.
 *
 * A Mesh instance stores vertex and index data on the GPU using:
 *  - VAO: remembers the vertex attribute configuration
 *  - VBO: stores vertex attributes
 *  - EBO: stores triangle indices
 *
 * The class is **move-only** to prevent multiple owners of GPU objects.
 * GPU resources are allocated in the constructor and freed in the destructor.
 */
class Mesh {
public:
    /// Array of vertex attributes.
    std::vector<Vertex> vertices;

    /// Triangle index buffer.
    std::vector<GLuint> indices;

    /// Vertex Array Object used to render the mesh.
    GLuint VAO{};

    // -------------------------------------------------------------------------
    // Copy / Move semantics
    // -------------------------------------------------------------------------

    /// Copying is disabled to avoid double-free of GPU resources.
    Mesh(const Mesh &) = delete;

    Mesh &operator=(const Mesh &) = delete;

    /**
     * @brief Constructs a mesh by uploading vertex / index data to the GPU.
     *
     * @param verticesIn Vertex attribute list (moved).
     * @param indicesIn  Index buffer (moved).
     *
     * The constructor takes ownership and initializes VAO, VBO, and EBO.
     */
    Mesh(std::vector<Vertex> verticesIn,
         std::vector<GLuint> indicesIn) noexcept
        : vertices(std::move(verticesIn)),
          indices(std::move(indicesIn)) {
        setupMesh();
    }

    /**
     * @brief Move constructor — transfers GPU resource ownership.
     *
     * After the move:
     *  - `move.VAO` becomes 0 (invalid)
     *  - this mesh owns the VAO/VBO/EBO
     */
    Mesh(Mesh &&move) noexcept
        : vertices(std::move(move.vertices)),
          indices(std::move(move.indices)),
          VAO(move.VAO), VBO(move.VBO), EBO(move.EBO) {
        move.VAO = 0; // moved-from state
    }

    /**
     * @brief Move assignment operator — frees current GPU resources and adopts the new ones.
     */
    Mesh &operator=(Mesh &&move) noexcept {
        if (this == &move) return *this;

        freeGPUResources();
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

    /**
     * @brief Destructor — releases VAO, VBO, and EBO.
     *
     * As per RAII, GPU resources are cleaned up automatically when the object
     * goes out of scope.
     */
    ~Mesh() noexcept {
        freeGPUResources();
    }

    /**
     * @brief Draws the mesh using glDrawElements().
     *
     * Binds the VAO and issues a draw call for indexed triangles.
     * The caller must already have a valid shader bound.
     */
    void Draw() const {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

private:
    /// Vertex Buffer Object (stores vertex attributes).
    GLuint VBO{};

    /// Element Buffer Object (index buffer).
    GLuint EBO{};

    /**
     * @brief Internal helper to create VAO/VBO/EBO and upload vertex/index data.
     *
     * Configures vertex attribute pointers for:
     *  - layout 0 : Position
     *  - layout 1 : Normal
     *  - layout 2 : TexCoords
     *  - layout 3 : Tangent
     *  - layout 4 : Bitangent
     */
    void setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(Vertex) * vertices.size()),
                     vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(GLuint) * indices.size()),
                     indices.data(), GL_STATIC_DRAW);

        // Position attribute (layout = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              static_cast<void *>(nullptr));

        // Normal attribute (layout = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, Normal)));

        // Texture coordinates (layout = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, TexCoords)));

        // Tangent (layout = 3)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, Tangent)));

        // Bitangent (layout = 4)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void *>(offsetof(Vertex, Bitangent)));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0); // EBO stays bound to VAO
    }

    /**
     * @brief Deletes VAO, VBO, and EBO if owned.
     *
     * Safe to call even on a moved-from mesh.
     */
    void freeGPUResources() const {
        if (VAO) {
            glDeleteVertexArrays(1, &VAO);
            glDeleteBuffers(1, &VBO);
            glDeleteBuffers(1, &EBO);
        }
    }
};
