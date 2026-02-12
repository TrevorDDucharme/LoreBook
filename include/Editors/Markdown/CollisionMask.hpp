#pragma once
#include <GL/glew.h>
#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// CollisionMask - Alpha-based collision texture with CL/GL interop
// ────────────────────────────────────────────────────────────────────

class CollisionMask {
public:
    CollisionMask();
    ~CollisionMask();
    
    // Non-copyable
    CollisionMask(const CollisionMask&) = delete;
    CollisionMask& operator=(const CollisionMask&) = delete;
    
    /// Initialize with OpenCL context for interop
    bool init(cl_context clContext);
    
    /// Resize the collision texture (recreates FBO)
    void resize(int width, int height);
    
    /// Cleanup resources
    void cleanup();
    
    // ── GL rendering interface ──
    
    /// Bind the collision FBO for rendering
    void bindForRendering();
    
    /// Unbind (restore previous FBO)
    void unbind();
    
    /// Clear the collision mask (set all to 0)
    void clear();
    
    // ── CPU readback (for emission points, debugging) ──
    
    /// Read collision texture back to CPU buffer
    void readback();
    
    /// Sample alpha at a given document coordinate (0-1 range)
    float sample(float x, float y) const;
    
    /// Check if a point is solid (alpha > threshold)
    bool solid(float x, float y, float threshold = 0.5f) const {
        return sample(x, y) > threshold;
    }
    
    /// Compute approximate surface normal at a point
    glm::vec2 surfaceNormal(float x, float y) const;
    
    /// Get emission points within a bounding box where alpha > threshold
    void getEmissionPoints(const glm::vec2& min, const glm::vec2& max,
                           float threshold, std::vector<glm::vec2>& outPoints,
                           int maxPoints = 100);
    
    // ── CL/GL interop ──
    
    /// Acquire the collision texture for OpenCL usage
    void acquireForCL(cl_command_queue queue);
    
    /// Release the collision texture back to OpenGL
    void releaseFromCL(cl_command_queue queue);
    
    /// Get the CL image for kernel usage
    cl_mem getCLImage() const { return m_clImage; }
    
    // ── Accessors ──
    
    GLuint getTexture() const { return m_texture; }
    GLuint getFBO() const { return m_fbo; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    bool isValid() const { return m_fbo != 0 && m_texture != 0; }
    bool hasCPUData() const { return !m_cpuBuffer.empty(); }
    const uint8_t* getCPUData() const { return m_cpuBuffer.data(); }
    size_t getCPUDataSize() const { return m_cpuBuffer.size(); }

private:
    void createCLImage();
    void destroyCLImage();
    
    GLuint m_fbo = 0;
    GLuint m_texture = 0;       // R8 texture storing alpha values
    
    cl_context m_clContext = nullptr;
    cl_mem m_clImage = nullptr;
    bool m_clAcquired = false;
    
    int m_width = 0;
    int m_height = 0;
    
    std::vector<uint8_t> m_cpuBuffer;
    GLint m_prevFBO = 0;
};

} // namespace Markdown
