#pragma once
#include <cmath>
#include <string>
#include <GL/glew.h>
#include <future>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>

class World;

class Projection {
public:
    Projection();
    virtual ~Projection();
    // Creates an OpenGL texture for the projected view and returns its GLuint.
    // NOTE: This must be called from a valid OpenGL context (GL thread).
    virtual GLuint project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName="", GLuint existingTexture = 0) const = 0;

    // Asynchronous pixel rendering: returns a future that will hold RGBA pixels (size width*height*4).
    // The future runs heavy sampling on background threads; caller must upload pixels to GL on the main thread when ready.
    // Optional tiling: if tileSize>0, the async task will render tiles of the given size and call progressCallback(tileX,tileY, tileW, tileH, pixels)
    // for each tile as it completes. If cancelToken is provided and set to true, the task should abort early and return an empty vector.
    virtual std::future<std::vector<uint8_t>> renderToPixelsAsync(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName, int tileSize = 0, std::function<void(int,int,int,int,const std::vector<uint8_t>&)> progressCallback = nullptr, std::shared_ptr<std::atomic_bool> cancelToken = nullptr) const;
};

class MercatorProjection : public Projection {
public:
    MercatorProjection();
    ~MercatorProjection() override;
    GLuint project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName, GLuint existingTexture) const override;
};