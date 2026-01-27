#include "WorldMaps/World/Projection.hpp"
#include "WorldMaps/World/World.hpp"
#include "WorldMaps/Map/MapLayer.hpp"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <future>
#include <thread>
#include <atomic>
#include <cstring>
#include <queue>
#include <condition_variable>
#include <functional>
#include <glm/glm.hpp>

// Simple thread pool for reusing worker threads across renders
class ThreadPool {
public:
    ThreadPool(unsigned int n = std::thread::hardware_concurrency()) : stop_(false) {
        unsigned int workers = std::max(1u, n);
        for(unsigned int i=0;i<workers;++i){
            workers_.emplace_back([this](){
                while(true){
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lg(this->mutex_);
                        this->cv_.wait(lg, [this]{ return this->stop_ || !this->tasks_.empty(); });
                        if(this->stop_ && this->tasks_.empty()) return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool(){
        {
            std::lock_guard<std::mutex> lg(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for(auto &t : workers_) if(t.joinable()) t.join();
    }

    template<typename F>
    auto enqueue(F&& f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto taskPtr = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> res = taskPtr->get_future();
        {
            std::lock_guard<std::mutex> lg(mutex_);
            tasks_.emplace([taskPtr](){ (*taskPtr)(); });
        }
        cv_.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_;
};

// shared pool instance for projection work
static ThreadPool s_projectionPool(std::thread::hardware_concurrency());

// Default constructors/destructors
Projection::Projection() = default;
Projection::~Projection()
{
    // Attempt to delete owned GL textures. This should ideally run on a valid GL context.
    if(frontTexOwned_ && frontTex_ != 0){
        glDeleteTextures(1, &frontTex_);
        frontTex_ = 0;
    }
    if(backTexOwned_ && backTex_ != 0){
        glDeleteTextures(1, &backTex_);
        backTex_ = 0;
    }
}

MercatorProjection::MercatorProjection() = default;
MercatorProjection::~MercatorProjection() = default;

SphericalProjection::SphericalProjection() = default;
SphericalProjection::~SphericalProjection() = default;

// Default AA samples
std::atomic<int> SphericalProjection::s_sphericalAASamples{1};

// Ensure pixel buffers are present and sized
void Projection::ensureBuffers(int width, int height) const {
    size_t required = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if(bufWidth_ != width || bufHeight_ != height){
        frontPixels_.assign(required, 0);
        backPixels_.assign(required, 0);
        bufWidth_ = width;
        bufHeight_ = height;
    } else {
        if(frontPixels_.size() < required) frontPixels_.assign(required, 0);
        if(backPixels_.size() < required) backPixels_.assign(required, 0);
    }
}

// Ensure GL textures exist; adopt existingTexture as front texture if supplied
void Projection::ensureTextures(int width, int height, GLuint existingTexture) const {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if(frontTex_ == 0 && existingTexture != 0){
        // adopt the provided texture as the front texture (not owned)
        frontTex_ = existingTexture;
        frontTexOwned_ = false;
    }

    // Create back texture if needed
    if(backTex_ == 0){
        glGenTextures(1, &backTex_);
        backTexOwned_ = true;
        glBindTexture(GL_TEXTURE_2D, backTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Allocate with null initially; we'll upload the actual pixels later
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // If we still don't have a front texture, create one (owned)
    if(frontTex_ == 0){
        glGenTextures(1, &frontTex_);
        frontTexOwned_ = true;
        glBindTexture(GL_TEXTURE_2D, frontTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

GLuint MercatorProjection::project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName, GLuint existingTexture) const {
    // Ensure valid dimensions
    if (width <= 0 || height <= 0) return 0;

    // Wait for any previous render to finish
    {
        std::unique_lock<std::mutex> lg(this->mutex_);
        this->cv_.wait(lg, [this]{ return !this->rendering_.load(); });
        // Mark as rendering while we perform the steps below
        this->rendering_.store(true);
    }

    // Prepare buffers and textures (on GL thread)
    ensureBuffers(width, height);
    ensureTextures(width, height, existingTexture);

    // Precompute center in projected (u,v) space using forward Mercator mapping
    float u_center = (longitude + 180.0f) / 360.0f;
    float lat_rad = latitude * static_cast<float>(M_PI) / 180.0f;
    float mercN_center = std::log(std::tan(static_cast<float>(M_PI)/4.0f + lat_rad/2.0f));
    float v_center = 0.5f * (1.0f - mercN_center / static_cast<float>(M_PI));

    // Tile the work by rows to allow concurrent sampling
    const int rowsPerTask = ROWS_PER_TASK;
    const int numTasks = (height + rowsPerTask - 1) / rowsPerTask;

    std::vector<std::future<void>> futures;
    futures.reserve(numTasks);

    for(int t=0; t<numTasks; ++t){
        int row0 = t * rowsPerTask;
        int row1 = std::min(height, row0 + rowsPerTask);

        // Capture by value the small set of immutable parameters; capture 'this' to access mutable buffers
        futures.emplace_back(s_projectionPool.enqueue([this, &world, u_center, v_center, zoomLevel, width, height, layerName, row0, row1]()->void{
            const size_t widthU = static_cast<size_t>(width);
            for(int i = row0; i < row1; ++i){
                for(int j = 0; j < width; ++j){
                    float nx = (static_cast<float>(j) + 0.5f) / static_cast<float>(width) - 0.5f; // [-0.5,0.5]
                    float ny = (static_cast<float>(i) + 0.5f) / static_cast<float>(height) - 0.5f; // [-0.5,0.5]
                    float u = u_center + nx / zoomLevel;
                    float v = v_center + ny / zoomLevel;

                    if (u < 0.0f) u = u - std::floor(u);
                    if (u >= 1.0f) u = u - std::floor(u);
                    constexpr float tiny = 1e-6f;
                    v = std::clamp(v, tiny, 1.0f - tiny);

                    float lon = u * 360.0f - 180.0f;
                    float mercN = static_cast<float>(M_PI) * (1.0f - 2.0f * v);
                    float lat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));

                    std::array<uint8_t,4> color = world.getColor(lon, lat, layerName);
                    size_t idx = (static_cast<size_t>(i) * widthU + static_cast<size_t>(j)) * 4u;
                    this->backPixels_[idx + 0] = color[0];
                    this->backPixels_[idx + 1] = color[1];
                    this->backPixels_[idx + 2] = color[2];
                    this->backPixels_[idx + 3] = color[3];
                }
            }
        }));
    }

    // Wait for all worker tasks to finish
    for(auto &f : futures){
        f.get();
    }

    // Upload back buffer to backTex (must be done on GL thread)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, this->backTex_);
    // Reallocate with full upload (safe and simple)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->backPixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // Swap front and back buffers/textures atomically
    {
        std::lock_guard<std::mutex> lg(this->mutex_);
        std::swap(this->frontPixels_, this->backPixels_);
        std::swap(this->frontTex_, this->backTex_);
        std::swap(this->frontTexOwned_, this->backTexOwned_);
        // Mark rendering complete and notify anyone waiting
        this->rendering_.store(false);
    }
    this->cv_.notify_all();

    // Return the texture which now contains the fully-filled front buffer
    return this->frontTex_;
}

// Screen-space spherical projection implementation
GLuint SphericalProjection::project(const World& world, float centerLon, float centerLat, float zoomLevel, int width, int height, std::string layerName, GLuint existingTexture) const {
    // Ensure valid dimensions
    if (width <= 0 || height <= 0) return 0;

    // Wait for any previous render to finish
    {
        std::unique_lock<std::mutex> lg(this->mutex_);
        this->cv_.wait(lg, [this]{ return !this->rendering_.load(); });
        this->rendering_.store(true);
    }

    ensureBuffers(width, height);
    ensureTextures(width, height, existingTexture);

    // Camera setup: map center lon/lat to a direction on the unit sphere
    float lonRad = centerLon * static_cast<float>(M_PI) / 180.0f;
    float latRad = centerLat * static_cast<float>(M_PI) / 180.0f;
    glm::vec3 centerDir(std::cos(latRad) * std::cos(lonRad), std::sin(latRad), std::cos(latRad) * std::sin(lonRad));

    // Camera distance controlled by zoomLevel (ensure outside unit sphere)
    float distance = 2.2f / (zoomLevel > 0.0f ? zoomLevel : 1.0f);
    if(distance < 1.05f) distance = 1.05f;
    glm::vec3 camPos = centerDir * distance;

    glm::vec3 forward = glm::normalize(-camPos);
    glm::vec3 upRef = std::fabs(forward.y) > 0.99f ? glm::vec3(0.0f,0.0f,1.0f) : glm::vec3(0.0f,1.0f,0.0f);
    glm::vec3 right = glm::normalize(glm::cross(upRef, forward));
    glm::vec3 up = glm::cross(forward, right);

    const float fovY = glm::radians(45.0f);
    const float tanFov2 = std::tan(fovY * 0.5f);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    const int aaSamples = std::max(1, SphericalProjection::s_sphericalAASamples.load());
    int aaDim = 1;
    while(aaDim * aaDim < aaSamples) ++aaDim;
    float invAA = 1.0f / static_cast<float>(aaDim);

    const int rowsPerTask = ROWS_PER_TASK;
    const int numTasks = (height + rowsPerTask - 1) / rowsPerTask;
    std::vector<std::future<void>> futures; futures.reserve(numTasks);

    const float camPosDot = glm::dot(camPos, camPos);

    for(int t=0; t<numTasks; ++t){
        int row0 = t * rowsPerTask; int row1 = std::min(height, row0 + rowsPerTask);
        futures.emplace_back(s_projectionPool.enqueue([this, &world, layerName, width, height, row0, row1, camPos, forward, right, up, tanFov2, aspect, aaDim, invAA, camPosDot](){
            const size_t widthU = static_cast<size_t>(width);
            for(int i = row0; i < row1; ++i){
                for(int j = 0; j < width; ++j){
                    // NDC coordinates
                    float ndcX = ((static_cast<float>(j) + 0.5f) / static_cast<float>(width)) * 2.0f - 1.0f;
                    float ndcY = 1.0f - ((static_cast<float>(i) + 0.5f) / static_cast<float>(height)) * 2.0f;

                    glm::vec3 accum = glm::vec3(0.0f);

                    for(int ay=0; ay<aaDim; ++ay){
                        for(int ax=0; ax<aaDim; ++ax){
                            // subpixel offsets in range [-0.5,0.5]
                            float ox = ( (ax + 0.5f) * invAA - 0.5f ) / static_cast<float>(width);
                            float oy = ( (ay + 0.5f) * invAA - 0.5f ) / static_cast<float>(height);

                            float sx = ndcX + ox * 2.0f;
                            float sy = ndcY + oy * 2.0f;

                            float px = sx * tanFov2 * aspect;
                            float py = sy * tanFov2;

                            glm::vec3 dir = glm::normalize(px * right + py * up + forward);

                            // Ray-sphere intersection (sphere radius = 1 at origin)
                            float d = glm::dot(camPos, dir);
                            float c = camPosDot - 1.0f;
                            float disc = d*d - c;
                            if(disc < 0.0f) continue; // miss, leave background
                            float root = std::sqrt(disc);
                            float tHit = -d - root;
                            if(tHit < 0.0f){ tHit = -d + root; if(tHit < 0.0f) continue; }
                            glm::vec3 hit = camPos + dir * tHit;
                            glm::vec3 n = glm::normalize(hit);

                            // Convert to lon/lat
                            float lat = std::asin(std::clamp(n.y, -1.0f, 1.0f)) * 180.0f / static_cast<float>(M_PI);
                            float lon = std::atan2(n.z, n.x) * 180.0f / static_cast<float>(M_PI);

                            std::array<uint8_t,4> color = world.getColor(lon, lat, layerName);
                            // simple diffuse lighting to add day/night shading
                            static const glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, 0.4f, 1.0f));
                            float ndl = std::max(0.0f, glm::dot(n, lightDir));
                            float shade = 0.2f + 0.8f * ndl;
                            accum += glm::vec3(color[0], color[1], color[2]) * shade;
                        }
                    }

                    // Average samples
                    int samples = aaDim * aaDim;
                    glm::vec3 finalc = accum / static_cast<float>(std::max(1, samples));
                    size_t idx = (static_cast<size_t>(i) * widthU + static_cast<size_t>(j)) * 4u;
                    this->backPixels_[idx + 0] = static_cast<uint8_t>(std::clamp(finalc.r, 0.0f, 255.0f));
                    this->backPixels_[idx + 1] = static_cast<uint8_t>(std::clamp(finalc.g, 0.0f, 255.0f));
                    this->backPixels_[idx + 2] = static_cast<uint8_t>(std::clamp(finalc.b, 0.0f, 255.0f));
                    this->backPixels_[idx + 3] = 255;
                }
            }
        }));
    }

    for(auto &f : futures) f.get();

    // Upload back buffer to backTex (GL thread)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, this->backTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->backPixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // Swap front/back
    {
        std::lock_guard<std::mutex> lg(this->mutex_);
        std::swap(this->frontPixels_, this->backPixels_);
        std::swap(this->frontTex_, this->backTex_);
        std::swap(this->frontTexOwned_, this->backTexOwned_);
        this->rendering_.store(false);
    }
    this->cv_.notify_all();

    return this->frontTex_;
}
