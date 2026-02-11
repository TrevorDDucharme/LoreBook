#include <Editors/Markdown/CollisionMask.hpp>
#include <plog/Log.h>
#include <cmath>
#include <random>

namespace Markdown {

CollisionMask::CollisionMask() = default;

CollisionMask::~CollisionMask() {
    cleanup();
}

bool CollisionMask::init(cl_context clContext) {
    m_clContext = clContext;
    return true;
}

void CollisionMask::resize(int width, int height) {
    if (width == m_width && height == m_height && m_fbo != 0) {
        return;  // No change needed
    }
    
    // Ensure GL operations are complete before cleanup
    glFinish();
    
    // Clean up CL resources first (they reference GL texture)
    destroyCLImage();
    
    // Clean up existing GL resources
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    
    m_width = width;
    m_height = height;
    // Don't shrink_to_fit here - it causes heap corruption during rapid resize
    // The buffer will be properly sized in readback()
    
    if (width <= 0 || height <= 0) {
        m_cpuBuffer.clear();
        return;
    }
    
    // Create R8 texture for alpha storage
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Create FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        PLOG_ERROR << "CollisionMask FBO incomplete: " << status;
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_texture);
        m_fbo = 0;
        m_texture = 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Ensure GL is done before potentially creating CL image
    glFinish();
    
    // CL interop is optional - don't create if previously failed
    // R8 format often isn't supported for CL/GL interop
    // The CPU readback path works fine without it
    
    PLOG_DEBUG << "CollisionMask resized to " << width << "x" << height;
}

void CollisionMask::cleanup() {
    // Ensure all operations complete before cleanup
    glFinish();
    
    // Release CL resources first (they depend on GL)
    destroyCLImage();
    
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    
    m_cpuBuffer.clear();
    m_width = 0;
    m_height = 0;
}

void CollisionMask::createCLImage() {
    if (!m_clContext || !m_texture || m_clImage) return;
    
    cl_int err;
    m_clImage = clCreateFromGLTexture(m_clContext, CL_MEM_READ_ONLY,
                                       GL_TEXTURE_2D, 0, m_texture, &err);
    if (err != CL_SUCCESS || !m_clImage) {
        PLOG_WARNING << "Failed to create CL image from collision texture: " << err;
        m_clImage = nullptr;
    }
}

void CollisionMask::destroyCLImage() {
    if (m_clImage) {
        clReleaseMemObject(m_clImage);
        m_clImage = nullptr;
    }
    m_clAcquired = false;
}

void CollisionMask::bindForRendering() {
    if (!m_fbo) return;
    
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prevFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void CollisionMask::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_prevFBO);
}

void CollisionMask::clear() {
    if (!m_fbo) return;
    
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
}

void CollisionMask::readback() {
    if (!m_texture || m_width <= 0 || m_height <= 0) {
        m_cpuBuffer.clear();
        return;
    }
    
    // Ensure proper buffer size
    size_t requiredSize = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (m_cpuBuffer.size() != requiredSize) {
        m_cpuBuffer.resize(requiredSize);
    }
    
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, m_cpuBuffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

float CollisionMask::sample(float x, float y) const {
    if (m_cpuBuffer.empty() || m_width <= 0 || m_height <= 0) {
        return 0.0f;
    }
    
    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);
    
    if (ix < 0 || ix >= m_width || iy < 0 || iy >= m_height) {
        return 0.0f;
    }
    
    size_t index = static_cast<size_t>(iy) * static_cast<size_t>(m_width) + static_cast<size_t>(ix);
    if (index >= m_cpuBuffer.size()) {
        return 0.0f;  // Safety check
    }
    
    return m_cpuBuffer[index] / 255.0f;
}

glm::vec2 CollisionMask::surfaceNormal(float x, float y) const {
    if (m_cpuBuffer.empty()) {
        return glm::vec2(0, -1);  // Default up
    }
    
    // Sample neighbors
    float alphaL = sample(x - 1, y);
    float alphaR = sample(x + 1, y);
    float alphaU = sample(x, y - 1);
    float alphaD = sample(x, y + 1);
    
    // Gradient points toward lower alpha (outward from solid)
    glm::vec2 gradient(alphaL - alphaR, alphaU - alphaD);
    
    float len = glm::length(gradient);
    if (len > 0.001f) {
        return gradient / len;
    }
    
    return glm::vec2(0, -1);  // Default up
}

void CollisionMask::getEmissionPoints(const glm::vec2& min, const glm::vec2& max,
                                       float threshold, std::vector<glm::vec2>& outPoints,
                                       int maxPoints) {
    outPoints.clear();
    
    if (m_cpuBuffer.empty() || m_width <= 0 || m_height <= 0) {
        return;
    }
    
    size_t expectedSize = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (m_cpuBuffer.size() != expectedSize) {
        return;  // Buffer size mismatch - don't risk it
    }
    
    // Clamp bounds
    int x0 = std::max(0, static_cast<int>(min.x));
    int y0 = std::max(0, static_cast<int>(min.y));
    int x1 = std::min(m_width - 1, static_cast<int>(max.x));
    int y1 = std::min(m_height - 1, static_cast<int>(max.y));
    
    // Collect candidate points where alpha > threshold
    std::vector<glm::vec2> candidates;
    uint8_t thresh8 = static_cast<uint8_t>(threshold * 255);
    
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
            if (index < m_cpuBuffer.size() && m_cpuBuffer[index] > thresh8) {
                candidates.push_back({static_cast<float>(x), static_cast<float>(y)});
            }
        }
    }
    
    // If too many candidates, randomly sample
    if (candidates.size() > static_cast<size_t>(maxPoints)) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(candidates.begin(), candidates.end(), gen);
        candidates.resize(maxPoints);
    }
    
    outPoints = std::move(candidates);
}

void CollisionMask::acquireForCL(cl_command_queue queue) {
    if (!m_clImage || m_clAcquired) return;
    
    // Ensure GL is done with the texture
    glFinish();
    
    cl_int err = clEnqueueAcquireGLObjects(queue, 1, &m_clImage, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_WARNING << "clEnqueueAcquireGLObjects failed: " << err;
        return;
    }
    
    m_clAcquired = true;
}

void CollisionMask::releaseFromCL(cl_command_queue queue) {
    if (!m_clImage || !m_clAcquired) return;
    
    cl_int err = clEnqueueReleaseGLObjects(queue, 1, &m_clImage, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_WARNING << "clEnqueueReleaseGLObjects failed: " << err;
    }
    
    m_clAcquired = false;
}

} // namespace Markdown
