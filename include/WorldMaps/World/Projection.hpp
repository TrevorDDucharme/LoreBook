#pragma once
#include <cmath>
#include <string>
#include <GL/glew.h>
#include <future>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <array>

class World;

class Projection {
public:
    Projection();
    virtual ~Projection();
    // Creates an OpenGL texture for the projected view and returns its GLuint.
    // NOTE: This must be called from a valid OpenGL context (GL thread).
    // This method blocks until a fully-filled texture is available and swaps internal buffers only when
    // the new buffer is complete. Project implementations should render into the internal back buffer
    // using worker threads and only upload/swap on completion.
    virtual GLuint project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName="", GLuint existingTexture = 0) const = 0;

protected:
    // Double-buffered pixel storage (RGBA8)
    mutable std::vector<uint8_t> frontPixels_;
    mutable std::vector<uint8_t> backPixels_;
    mutable int bufWidth_ = 0;
    mutable int bufHeight_ = 0;

    // GL textures corresponding to front/back buffers
    mutable GLuint frontTex_ = 0;
    mutable GLuint backTex_ = 0;
    mutable bool frontTexOwned_ = false;
    mutable bool backTexOwned_ = false;

    // Synchronization for in-flight rendering
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::atomic<bool> rendering_{false};

    // Helper to ensure buffers and textures are prepared (to be implemented in .cpp)
    void ensureBuffers(int width, int height) const;
    void ensureTextures(int width, int height, GLuint existingTexture) const;

    static constexpr int ROWS_PER_TASK = 8; // tuning knob for tile size
};

class MercatorProjection : public Projection {
public:
    MercatorProjection();
    ~MercatorProjection() override;
    GLuint project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName, GLuint existingTexture) const override;
};