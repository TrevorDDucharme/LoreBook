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
Projection::~Projection() = default;

MercatorProjection::MercatorProjection() = default;
MercatorProjection::~MercatorProjection() = default;

GLuint MercatorProjection::project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName, GLuint existingTexture) const {
    // Ensure valid dimensions
    if (width <= 0 || height <= 0) return 0;

    // Prepare an RGBA8 buffer
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> pixels(pixelCount * 4, 0);

    // Precompute center in projected (u,v) space using forward Mercator mapping
    // u in [0,1] maps to longitude [-180,180].
    float u_center = (longitude + 180.0f) / 360.0f;
    float lat_rad = latitude * static_cast<float>(M_PI) / 180.0f;
    float mercN_center = std::log(std::tan(static_cast<float>(M_PI)/4.0f + lat_rad/2.0f));
    float v_center = 0.5f * (1.0f - mercN_center / static_cast<float>(M_PI));

    // Fill the buffer by sampling the world after projecting to Mercator then zooming in 2D projected plane
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            // Compute normalized offsets centered at 0 in projected plane and scale by zoom
            float nx = (static_cast<float>(j) + 0.5f) / static_cast<float>(width) - 0.5f; // [-0.5,0.5]
            float ny = (static_cast<float>(i) + 0.5f) / static_cast<float>(height) - 0.5f; // [-0.5,0.5]
            float u = u_center + nx / zoomLevel;
            float v = v_center + ny / zoomLevel;

            // Handle wrap-around horizontally (longitude)
            if (u < 0.0f) u = u - std::floor(u);
            if (u >= 1.0f) u = u - std::floor(u);
            // Clamp v to [tiny, 1-tiny] to avoid extreme singularities at the poles
            constexpr float tiny = 1e-6f;
            v = std::clamp(v, tiny, 1.0f - tiny);

            // Map u to longitude in [-180,180]
            float lon = u * 360.0f - 180.0f;
            // Mercator inverse to latitude
            float mercN = static_cast<float>(M_PI) * (1.0f - 2.0f * v);
            float lat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));

            // Sample world (returns channels in [-1,1] typically)
            std::array<uint8_t, 4> color = world.getColor(lon, lat, layerName);
            // If the requested layer is the composite 'color' layer, it will access other layers internally.
            size_t idx = (static_cast<size_t>(i) * static_cast<size_t>(width) + static_cast<size_t>(j)) * 4;
            pixels[idx + 0] = color[0];
            pixels[idx + 1] = color[1];
            pixels[idx + 2] = color[2];
            pixels[idx + 3] = color[3];
        }
    }

    // Create or update GL texture and upload buffer. Prefer updating an existing texture to avoid reallocation.
    GLuint textureID = existingTexture;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // ensure tight packing

    if(textureID == 0){
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // allocate and upload
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    } else {
        // update existing texture content
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }

    // Unbind texture
    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}

std::future<std::vector<uint8_t>> Projection::renderToPixelsAsync(const World& world, float longitude, float latitude, float zoomLevel, int width, int height, std::string layerName, int tileSize, std::function<void(int,int,int,int,const std::vector<uint8_t>&)> progressCallback, std::shared_ptr<std::atomic_bool> cancelToken) const {
    // Launch an async task that fills the pixel buffer potentially using multiple worker threads
    return std::async(std::launch::async, [this, &world, longitude, latitude, zoomLevel, width, height, layerName, tileSize, progressCallback, cancelToken]() -> std::vector<uint8_t> {
        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uint8_t> pixels(pixelCount * 4, 0);

        // Precompute center in projected (u,v) space using forward Mercator mapping
        float u_center = (longitude + 180.0f) / 360.0f;
        float lat_rad = latitude * static_cast<float>(M_PI) / 180.0f;
        float mercN_center = std::log(std::tan(static_cast<float>(M_PI)/4.0f + lat_rad/2.0f));
        float v_center = 0.5f * (1.0f - mercN_center / static_cast<float>(M_PI));

        // Helper to check cancellation
        auto isCancelled = [&]() -> bool { return cancelToken && cancelToken->load(); };

        // Special 'river' generation requires full elevation grid and routing
        if(layerName == "river" && tileSize <= 0){
            std::vector<float> elevGrid(width * height, 0.0f);
            for (int i = 0; i < height; ++i){
                if(isCancelled()) return std::vector<uint8_t>();
                for (int j = 0; j < width; ++j){
                    float nx = (static_cast<float>(j) + 0.5f) / static_cast<float>(width) - 0.5f;
                    float ny = (static_cast<float>(i) + 0.5f) / static_cast<float>(height) - 0.5f;
                    float u = u_center + nx / zoomLevel;
                    float v = v_center + ny / zoomLevel;
                    if (u < 0.0f) u = u - std::floor(u);
                    if (u >= 1.0f) u = u - std::floor(u);
                    constexpr float tiny = 1e-6f;
                    v = std::clamp(v, tiny, 1.0f - tiny);
                    float lon = u * 360.0f - 180.0f;
                    float mercN = static_cast<float>(M_PI) * (1.0f - 2.0f * v);
                    float lat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));
                    SampleData s = world.sample(lon, lat, "elevation");
                    elevGrid[i * width + j] = (s.channels.empty() ? 0.0f : s.channels[0]);
                }
            }

            // Flow routing using path-following (watershed-like) per cell
            std::vector<float> accum(width * height, 0.0f);
            for (int i = 0; i < height; ++i){
                if(isCancelled()) return std::vector<uint8_t>();
                for (int j = 0; j < width; ++j){
                    int ci = i, cj = j;
                    std::vector<std::pair<int,int>> path;
                    path.emplace_back(ci,cj);
                    for(int step=0; step < (width*height); ++step){
                        float best = elevGrid[ci*width + cj];
                        int bi = ci, bj = cj;
                        for(int di=-1; di<=1; ++di){
                            for(int dj=-1; dj<=1; ++dj){
                                if(di==0 && dj==0) continue;
                                int ni = ci + di; int nj = cj + dj;
                                if(ni < 0 || nj < 0 || ni >= height || nj >= width) continue;
                                float val = elevGrid[ni*width + nj];
                                if(val < best){ best = val; bi = ni; bj = nj; }
                            }
                        }
                        if(bi==ci && bj==cj) break; // local minima
                        ci = bi; cj = bj;
                        if(std::find(path.begin(), path.end(), std::make_pair(ci,cj)) != path.end()) break; // loop
                        path.emplace_back(ci,cj);
                    }
                    for(auto &p : path) accum[p.first*width + p.second] += 1.0f;
                }
            }
            float maxA = 0.0f; for(auto &v : accum) maxA = std::max(maxA, v);
            for (int i = 0; i < height; ++i){
                if(isCancelled()) return std::vector<uint8_t>();
                for (int j = 0; j < width; ++j){
                    float a = (maxA > 0.0f) ? accum[i*width + j] / maxA : 0.0f;
                    size_t idx = (static_cast<size_t>(i) * static_cast<size_t>(width) + static_cast<size_t>(j)) * 4;
                    if(a > 0.002f){
                        uint8_t v = static_cast<uint8_t>(std::min(255.0f, 100.0f + a * 155.0f));
                        pixels[idx+0] = 10; pixels[idx+1] = 120; pixels[idx+2] = v; pixels[idx+3] = 255;
                    } else {
                        pixels[idx+0] = 0; pixels[idx+1] = 0; pixels[idx+2] = 0; pixels[idx+3] = 0;
                    }
                }
            }
            return pixels;
        }

        // If tiling requested, render tile-by-tile and call progressCallback for each completed tile
        if(tileSize > 0){
            int tilesX = (width + tileSize - 1) / tileSize;
            int tilesY = (height + tileSize - 1) / tileSize;

            // Build an interleaved row order that alternates top and bottom rows to avoid starving bottom tiles
            std::vector<int> rowOrder;
            rowOrder.reserve(tilesY);
            for(int k=0; k < tilesY/2; ++k){
                rowOrder.push_back(k);
                rowOrder.push_back(tilesY - 1 - k);
            }
            if(tilesY % 2 == 1) rowOrder.push_back(tilesY/2);

            for(int rIdx = 0; rIdx < (int)rowOrder.size(); ++rIdx){
                int ty = rowOrder[rIdx];
                for(int tx=0; tx<tilesX; ++tx){
                    if(isCancelled()) return std::vector<uint8_t>();
                    int x0 = tx * tileSize;
                    int y0 = ty * tileSize;
                    int w = std::min(tileSize, width - x0);
                    int h = std::min(tileSize, height - y0);

                    std::vector<uint8_t> tilePixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);

                    // Parallelize rows within the tile using the shared thread pool
                    std::vector<std::future<void>> rowTasks;
                    rowTasks.reserve(h);
                    for(int i=0;i<h;++i){
                        rowTasks.emplace_back(s_projectionPool.enqueue([&, i]() {
                            if(isCancelled()) return;
                            int global_i = y0 + i;
                            for (int j = 0; j < w; ++j) {
                                int global_j = x0 + j;
                                float nx = (static_cast<float>(global_j) + 0.5f) / static_cast<float>(width) - 0.5f;
                                float ny = (static_cast<float>(global_i) + 0.5f) / static_cast<float>(height) - 0.5f;
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
                                size_t idx = (static_cast<size_t>(i) * static_cast<size_t>(w) + static_cast<size_t>(j)) * 4;
                                tilePixels[idx + 0] = color[0];
                                tilePixels[idx + 1] = color[1];
                                tilePixels[idx + 2] = color[2];
                                tilePixels[idx + 3] = color[3];
                            }
                        }));
                    }
                    for(auto &f : rowTasks) f.get();

                    // Copy tilePixels into main pixels buffer
                    for(int yi=0; yi<h; ++yi){
                        int global_i = y0 + yi;
                        size_t destOffset = static_cast<size_t>(global_i) * static_cast<size_t>(width) * 4 + static_cast<size_t>(x0) * 4;
                        size_t srcOffset = static_cast<size_t>(yi) * static_cast<size_t>(w) * 4;
                        memcpy(pixels.data() + destOffset, tilePixels.data() + srcOffset, static_cast<size_t>(w) * 4);
                    }

                    // Notify progress if requested
                    if(progressCallback){
                        progressCallback(x0, y0, w, h, tilePixels);
                    }

                }
            }
            return pixels;
        }

        // Parallel row-based sampling for other layers (non-tiling) using the shared thread pool
        std::vector<std::future<void>> rowTasks;
        rowTasks.reserve(height);
        for(int i=0;i<height;++i){
            rowTasks.emplace_back(s_projectionPool.enqueue([&, i]() {
                if(isCancelled()) return;
                for (int j = 0; j < width; ++j) {
                    float nx = (static_cast<float>(j) + 0.5f) / static_cast<float>(width) - 0.5f;
                    float ny = (static_cast<float>(i) + 0.5f) / static_cast<float>(height) - 0.5f;
                    float u = u_center + nx / zoomLevel;
                    float v = v_center + ny / zoomLevel;
                    if (u < 0.0f) u = u - std::floor(u);
                    if (u >= 1.0f) u = u - std::floor(u);
                    constexpr float tiny = 1e-6f;
                    v = std::clamp(v, tiny, 1.0f - tiny);
                    float lon = u * 360.0f - 180.0f;
                    float mercN = static_cast<float>(M_PI) * (1.0f - 2.0f * v);
                    float lat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));
                    std::array<uint8_t, 4> color = world.getColor(lon, lat, layerName);
                    size_t idx = (static_cast<size_t>(i) * static_cast<size_t>(width) + static_cast<size_t>(j)) * 4;
                    pixels[idx + 0] = color[0];
                    pixels[idx + 1] = color[1];
                    pixels[idx + 2] = color[2];
                    pixels[idx + 3] = color[3];
                }
            }));
        }
        for(auto &f : rowTasks) f.get();
        return pixels;
    });
}
