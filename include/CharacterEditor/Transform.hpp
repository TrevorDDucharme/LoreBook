#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace CharacterEditor {

/**
 * @brief Transform representing position, rotation, and scale in 3D space.
 * 
 * Uses glm types for compatibility with the rest of the graphics pipeline.
 */
struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Identity quaternion (w, x, y, z)
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    Transform() = default;
    
    Transform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}

    /**
     * @brief Convert transform to a 4x4 matrix (TRS order: translate * rotate * scale)
     */
    glm::mat4 toMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 r = glm::mat4_cast(rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }

    /**
     * @brief Create transform from a 4x4 matrix (decomposes TRS)
     */
    static Transform fromMatrix(const glm::mat4& m) {
        Transform result;
        
        // Extract translation
        result.position = glm::vec3(m[3]);
        
        // Extract scale (length of each basis vector)
        result.scale.x = glm::length(glm::vec3(m[0]));
        result.scale.y = glm::length(glm::vec3(m[1]));
        result.scale.z = glm::length(glm::vec3(m[2]));
        
        // Remove scale from rotation matrix
        glm::mat3 rotMat(
            glm::vec3(m[0]) / result.scale.x,
            glm::vec3(m[1]) / result.scale.y,
            glm::vec3(m[2]) / result.scale.z
        );
        result.rotation = glm::quat_cast(rotMat);
        
        return result;
    }

    /**
     * @brief Compose this transform with a child transform (this * child)
     */
    Transform compose(const Transform& child) const {
        Transform result;
        result.position = position + rotation * (scale * child.position);
        result.rotation = rotation * child.rotation;
        result.scale = scale * child.scale;
        return result;
    }

    /**
     * @brief Get the inverse of this transform
     */
    Transform inverse() const {
        Transform result;
        glm::quat invRot = glm::inverse(rotation);
        result.rotation = invRot;
        result.scale = 1.0f / scale;
        result.position = invRot * (-position / scale);
        return result;
    }

    /**
     * @brief Linearly interpolate between two transforms
     */
    static Transform lerp(const Transform& a, const Transform& b, float t) {
        Transform result;
        result.position = glm::mix(a.position, b.position, t);
        result.rotation = glm::slerp(a.rotation, b.rotation, t);
        result.scale = glm::mix(a.scale, b.scale, t);
        return result;
    }

    /**
     * @brief Transform a point from local to world space
     */
    glm::vec3 transformPoint(const glm::vec3& point) const {
        return position + rotation * (scale * point);
    }

    /**
     * @brief Transform a direction (ignores translation and scale)
     */
    glm::vec3 transformDirection(const glm::vec3& dir) const {
        return rotation * dir;
    }

    /**
     * @brief Transform a vector (ignores translation, applies scale)
     */
    glm::vec3 transformVector(const glm::vec3& vec) const {
        return rotation * (scale * vec);
    }

    bool operator==(const Transform& other) const {
        return position == other.position && 
               rotation == other.rotation && 
               scale == other.scale;
    }

    bool operator!=(const Transform& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Defines a local coordinate system for socket alignment.
 */
struct OrientationFrame {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};      // Primary axis (usually "up" or "out")
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};     // Secondary axis
    glm::vec3 bitangent{0.0f, 0.0f, 1.0f};   // Computed or explicit (for surface sockets)

    OrientationFrame() = default;
    
    OrientationFrame(const glm::vec3& n, const glm::vec3& t, const glm::vec3& b)
        : normal(n), tangent(t), bitangent(b) {}

    /**
     * @brief Convert to a 3x3 basis matrix
     */
    glm::mat3 toBasis() const {
        return glm::mat3(tangent, normal, bitangent);
    }

    /**
     * @brief Create from normal and tangent (computes bitangent)
     */
    static OrientationFrame fromNormalAndTangent(const glm::vec3& n, const glm::vec3& t) {
        OrientationFrame frame;
        frame.normal = glm::normalize(n);
        frame.tangent = glm::normalize(t - glm::dot(t, frame.normal) * frame.normal);
        frame.bitangent = glm::cross(frame.normal, frame.tangent);
        return frame;
    }

    /**
     * @brief Create from a rotation quaternion
     */
    static OrientationFrame fromQuaternion(const glm::quat& q) {
        OrientationFrame frame;
        frame.tangent = q * glm::vec3(1.0f, 0.0f, 0.0f);
        frame.normal = q * glm::vec3(0.0f, 1.0f, 0.0f);
        frame.bitangent = q * glm::vec3(0.0f, 0.0f, 1.0f);
        return frame;
    }

    /**
     * @brief Convert to a quaternion
     */
    glm::quat toQuaternion() const {
        return glm::quat_cast(toBasis());
    }
};

} // namespace CharacterEditor