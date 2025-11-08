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

#include <utils/mesh.h>

using namespace std;

class Model {
public:
    vector<Mesh> meshes;

    // Disable copying (move-only class)
    Model(const Model &) = delete;

    Model &operator=(const Model &) = delete;

    // Enable move operations
    Model(Model &&) = default;

    Model &operator=(Model &&) noexcept = default;

    // Constructor loads the model
    explicit Model(const string &path) {
        loadModel(path);
    }

    // Renders all meshes in the model
    void Draw() const {
        for (auto &mesh: meshes)
            mesh.Draw();
    }

private:
    // Loads a model file using Assimp
    void loadModel(const string &path) {
        Assimp::Importer importer;

        const aiScene *scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_FlipUVs |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace
        );

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return;
        }

        processNode(scene->mRootNode, scene);
    }

    // Recursively processes nodes in the Assimp scene graph
    void processNode(const aiNode *node, const aiScene *scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.emplace_back(processMesh(mesh));
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], scene);
    }

    // Converts an aiMesh to a Mesh object
    static Mesh processMesh(aiMesh *mesh) {
        vector<Vertex> vertices;
        vector<GLuint> indices;

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex vertex{};

            // Position
            vertex.Position = {
                mesh->mVertices[i].x,
                mesh->mVertices[i].y,
                mesh->mVertices[i].z
            };

            // Normal
            vertex.Normal = {
                mesh->mNormals[i].x,
                mesh->mNormals[i].y,
                mesh->mNormals[i].z
            };

            // Texture Coordinates, Tangent, Bitangent (if available)
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoords = {
                    mesh->mTextureCoords[0][i].x,
                    mesh->mTextureCoords[0][i].y
                };
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
                vertex.TexCoords = glm::vec2(0.0f);
                vertex.Tangent = glm::vec3(0.0f);
                vertex.Bitangent = glm::vec3(0.0f);
                cout << "WARNING: Model lacks UVs — Tangent/Bitangent set to 0." << endl;
            }

            vertices.emplace_back(vertex);
        }

        // Extract vertex indices for each face
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            aiFace &face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
                indices.emplace_back(face.mIndices[j]);
        }

        return Mesh{vertices, indices};
    }
};
