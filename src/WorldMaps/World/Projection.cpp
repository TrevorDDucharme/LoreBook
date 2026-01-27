#include "WorldMaps/World/Projection.hpp"
#include "WorldMaps/World/World.hpp"
#include "WorldMaps/Map/MapLayer.hpp"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

// Default constructors/destructors
Projection::Projection() = default;
Projection::~Projection() = default;

MercatorProjection::MercatorProjection() = default;
MercatorProjection::~MercatorProjection() = default;

GLuint MercatorProjection::project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName) const {
    // Ensure valid dimensions
    if (width <= 0 || height <= 0) return 0;

    // Prepare an RGBA8 buffer
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> pixels(pixelCount * 4, 0);

    // Fill the buffer by sampling the world
    for (int i = 0; i < height; ++i) {
        float v = (static_cast<float>(i) + 0.5f) / static_cast<float>(height); // [0,1]
        for (int j = 0; j < width; ++j) {
            float u = (static_cast<float>(j) + 0.5f) / static_cast<float>(width); // [0,1]

            // Map u to longitude in [-180,180]
            float lon = u * 360.0f - 180.0f;
            // Mercator inverse to latitude
            float mercN = M_PI * (1.0f - 2.0f * v);
            float lat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));

            // Apply zoom by sampling a reduced range around center longitude/latitude if zoomLevel != 1
            // A simple approach: zoom scales the longitude/latitude offsets from the provided center
            float dlon = lon - longitude;
            float dlat = lat - latitude;
            lon = longitude + dlon / zoomLevel;
            lat = latitude + dlat / zoomLevel;

            // Sample world (returns channels in [-1,1] typically)
            std::array<uint8_t, 4> color = world.getColor(lon, lat, layerName);

            size_t idx = (static_cast<size_t>(i) * static_cast<size_t>(width) + static_cast<size_t>(j)) * 4;
            pixels[idx + 0] = color[0];
            pixels[idx + 1] = color[1];
            pixels[idx + 2] = color[2];
            pixels[idx + 3] = color[3];
        }
    }

    // Create GL texture and upload once
    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Ensure proper unpack alignment for tightly packed RGBA
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload the full buffer once (internal format RGBA8)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Unbind texture (leave it ready for use)
    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}
