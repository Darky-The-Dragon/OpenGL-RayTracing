/*
    Model class

    Loads .obj models using the Assimp library and converts the imported data into a format
    compatible with OpenGL rendering via the Mesh class.

    Key Concepts:
    - Uses Assimp to parse 3D model files
    - Converts Assimp mesh data to OpenGL-compatible structures
    - Stores each sub-mesh as a Mesh instance
    - Delegates rendering to Mesh::Draw()

    Notes:
    1) Follows RAII: Model and Mesh manage their own GPU resources and cleanup automatically.
    2) Copying is disabled — this is a move-only class to avoid duplicating GPU resources.
    3) This version does not support textures.
    4) Based on LearnOpenGL’s model loading example by Joey de Vries.

    Authors: Davide Gadia, Michael Marchesan
    Real-Time Graphics Programming – 2024/2025
    University of Milan
*/

#pragma once
#include <iostream>
#include <vector>
#include <string>

#include <glm/glm.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <scene/mesh.h>

/**
 * @class Model
 * @brief Loads and stores a 3D model composed of one or more Mesh objects.
 *
 * This class wraps the model import pipeline using **Assimp**, converting
 * imported meshes into the local Mesh representation. Each Assimp mesh becomes
 * a separate Mesh instance with its own VBO/EBO/VAO stored on the GPU.
 *
 * The class is **move-only** to ensure safe ownership of GPU resources.
 * It follows standard RAII: destruction automatically frees all GPU buffers
 * contained in its Mesh sub-objects.
 */
class Model {
public:
    /// List of sub-meshes forming this model.
    std::vector<Mesh> meshes;

    // -------------------------------------------------------------------------
    // Copy / Move semantics
    // -------------------------------------------------------------------------

    /// Copying is disabled to avoid duplicating heavy GPU resources.
    Model(const Model &) = delete;

    Model &operator=(const Model &) = delete;

    /// Move constructor — safe resource transfer.
    Model(Model &&) = default;

    /// Move assignment — safe resource transfer.
    Model &operator=(Model &&) noexcept = default;

    /**
     * @brief Constructs a model by loading it from disk.
     *
     * @param path Path to the model file (OBJ, FBX, etc. supported by Assimp).
     *
     * The constructor immediately calls loadModel() and populates the `meshes` vector.
     */
    explicit Model(const std::string &path) {
        loadModel(path);
    }

    /**
     * @brief Draws all meshes contained in this model.
     *
     * The actual rendering is delegated to each Mesh::Draw() call.
     */
    void Draw() const {
        for (const auto &mesh: meshes)
            mesh.Draw();
    }

private:
    // -------------------------------------------------------------------------
    // Import pipeline
    // -------------------------------------------------------------------------

    /**
     * @brief Loads a model file using Assimp and processes its entire node hierarchy.
     *
     * @param path Filesystem path to the model being loaded.
     *
     * The function:
     *  - reads the file via Assimp::Importer
     *  - validates the scene
     *  - recursively processes all nodes and meshes
     */
    void loadModel(const std::string &path) {
        Assimp::Importer importer;

        const aiScene *scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_FlipUVs |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace
        );

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            std::cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << '\n';
            return;
        }

        processNode(scene->mRootNode, scene);
    }

    /**
     * @brief Recursively walks the Assimp scene graph and processes each mesh.
     *
     * @param node  Current node in the scene hierarchy.
     * @param scene Full Assimp scene containing mesh references.
     *
     * Each referenced aiMesh is converted into a Mesh instance and stored
     * in the `meshes` vector.
     */
    void processNode(const aiNode *node, const aiScene *scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.emplace_back(processMesh(mesh));
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], scene);
    }

    /**
     * @brief Converts an Assimp aiMesh into a Mesh object.
     *
     * This function:
     *  - reads vertex attributes (pos, normal, UV, tangent, bitangent)
     *  - extracts triangle indices
     *  - fills std::vector<Vertex> and std::vector<GLuint>
     *  - constructs a Mesh object (which uploads the data to the GPU)
     *
     * @param mesh Assimp mesh structure containing raw mesh data.
     * @return Mesh converted to the internal format.
     */
    static Mesh processMesh(aiMesh *mesh) {
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;

        bool warnedNoUV = false;

        vertices.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex vertex{};

            // Position
            vertex.Position = {
                mesh->mVertices[i].x,
                mesh->mVertices[i].y,
                mesh->mVertices[i].z
            };

            // Normal
            if (mesh->HasNormals()) {
                vertex.Normal = {
                    mesh->mNormals[i].x,
                    mesh->mNormals[i].y,
                    mesh->mNormals[i].z
                };
            } else {
                vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // Texture coordinates, tangent, bitangent
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoords = {
                    mesh->mTextureCoords[0][i].x,
                    mesh->mTextureCoords[0][i].y
                };
                if (mesh->mTangents && mesh->mBitangents) {
                    vertex.Tangent = {
                        mesh->mTangents[i].x,
                        mesh->mTangents[i].y,
                        mesh->mTangents[i].z
                    };
                    vertex.Bitangent = {
                        mesh->mBitangents[i].x,
                        mesh->mBitangents[i].y,
                        mesh->mBitangents[i].z
                    };
                } else {
                    vertex.Tangent = glm::vec3(0.0f);
                    vertex.Bitangent = glm::vec3(0.0f);
                }
            } else {
                vertex.TexCoords = glm::vec2(0.0f);
                vertex.Tangent = glm::vec3(0.0f);
                vertex.Bitangent = glm::vec3(0.0f);
                if (!warnedNoUV) {
                    std::cout << "WARNING: Model lacks UVs — Tangent/Bitangent set to 0.\n";
                    warnedNoUV = true;
                }
            }

            vertices.emplace_back(vertex);
        }

        // Read all face indices
        indices.reserve(mesh->mNumFaces * 3);
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            aiFace &face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
                indices.emplace_back(face.mIndices[j]);
        }

        // Ownership and GPU upload occur inside Mesh
        return Mesh{std::move(vertices), std::move(indices)};
    }
};
