#include "cubemap.h"
#include <vector>
#include "stb_image.h"

// cubemap.cpp
GLuint createDummyCubeMap() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    const unsigned char px[4] = {128, 128, 255, 255}; // neutral-ish sky

    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                     0,
                     GL_RGBA8,
                     1, 1,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     px);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}

GLuint loadCubeMapFromCross(const std::string &path) {
    int width, height, channels;

    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (!data) {
        fprintf(stderr, "Failed to load cubemap cross image: %s\n", path.c_str());
        return 0;
    }

    // Expect a 4x3 cross layout
    if ((height % 3) != 0 || (width % 4) != 0 || (width / 4) != (height / 3)) {
        fprintf(stderr, "Invalid cubemap cross dimensions: %dx%d (expected 4x3 tiles)\n",
                width, height);
        stbi_image_free(data);
        return 0;
    }

    const int faceSize = height / 3;
    const int stride = width * channels;
    const GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    const GLenum internalFormat = (channels == 4) ? GL_RGBA8 : GL_RGB8;

    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

    std::vector<unsigned char> faceBuf(faceSize * faceSize * channels);

    auto uploadFace = [&](const GLenum faceTarget, int ox, int oy) {
        for (int y = 0; y < faceSize; ++y) {
            const unsigned char *srcRow = data + (oy + y) * stride + ox * channels;
            unsigned char *dstRow = faceBuf.data() + y * faceSize * channels;
            memcpy(dstRow, srcRow, faceSize * channels);
        }

        glTexImage2D(faceTarget, 0, internalFormat, faceSize, faceSize, 0, format,GL_UNSIGNED_BYTE, faceBuf.data());
    };

    // Layout:
    //   [   ][ +Y][   ][   ]
    //   [-X][ +Z][ +X][ -Z]
    //   [   ][ -Y][   ][   ]
    uploadFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 2 * faceSize, 1 * faceSize);
    uploadFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0 * faceSize, 1 * faceSize);
    uploadFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 1 * faceSize, 0 * faceSize);
    uploadFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 1 * faceSize, 2 * faceSize);
    uploadFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 1 * faceSize, 1 * faceSize);
    uploadFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 3 * faceSize, 1 * faceSize);

    stbi_image_free(data);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    return texID;
}
