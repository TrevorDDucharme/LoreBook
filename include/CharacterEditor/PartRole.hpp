#pragma once
#include <string>
#include <glm/glm.hpp>
#include <map>
#include <cstdint>

namespace CharacterEditor {

/**
 * @brief Anatomical role classification for parts and bones.
 * 
 * Part roles affect IK solving behavior, default constraints, and procedural animation.
 */
enum class PartRole : uint32_t {
    // Core body
    Root,               // Pelvis, center of mass
    Spine,              // Vertebral segments
    Chest,              // Ribcage/thorax
    Neck,               // Cervical spine
    Head,               // Skull + face
    
    // Upper limb
    Shoulder,           // Clavicle/scapula
    UpperArm,           // Humerus
    LowerArm,           // Radius/ulna
    Hand,               // Carpals + metacarpals
    Finger,             // Phalanges (digits)
    Thumb,              // Thumb (special 2-DOF base)
    
    // Lower limb
    Hip,                // Pelvis attachment
    UpperLeg,           // Femur
    LowerLeg,           // Tibia/fibula
    Foot,               // Tarsals + metatarsals
    Toe,                // Phalanges (digits)
    
    // Appendages
    Tail,               // Caudal vertebrae
    TailTip,            // End of tail
    WingRoot,           // Wing attachment
    WingMid,            // Wing elbow/wrist
    WingTip,            // Wing tip
    WingMembrane,       // Membrane between bones
    
    // Specialized
    Ear,                // Ear (for expressive characters)
    Jaw,                // Mandible
    Eye,                // Eye (for look-at)
    Tentacle,           // Tentacle segment
    TentacleTip,        // Tentacle end
    
    // Generic
    Appendage,          // Unspecified appendage
    Accessory,          // Non-anatomical attachment
    Custom,             // User-defined
    
    Count               // Number of roles (for iteration)
};

/**
 * @brief Get the string name of a PartRole
 */
inline const char* partRoleToString(PartRole role) {
    switch (role) {
        case PartRole::Root:        return "Root";
        case PartRole::Spine:       return "Spine";
        case PartRole::Chest:       return "Chest";
        case PartRole::Neck:        return "Neck";
        case PartRole::Head:        return "Head";
        case PartRole::Shoulder:    return "Shoulder";
        case PartRole::UpperArm:    return "UpperArm";
        case PartRole::LowerArm:    return "LowerArm";
        case PartRole::Hand:        return "Hand";
        case PartRole::Finger:      return "Finger";
        case PartRole::Thumb:       return "Thumb";
        case PartRole::Hip:         return "Hip";
        case PartRole::UpperLeg:    return "UpperLeg";
        case PartRole::LowerLeg:    return "LowerLeg";
        case PartRole::Foot:        return "Foot";
        case PartRole::Toe:         return "Toe";
        case PartRole::Tail:        return "Tail";
        case PartRole::TailTip:     return "TailTip";
        case PartRole::WingRoot:    return "WingRoot";
        case PartRole::WingMid:     return "WingMid";
        case PartRole::WingTip:     return "WingTip";
        case PartRole::WingMembrane: return "WingMembrane";
        case PartRole::Ear:         return "Ear";
        case PartRole::Jaw:         return "Jaw";
        case PartRole::Eye:         return "Eye";
        case PartRole::Tentacle:    return "Tentacle";
        case PartRole::TentacleTip: return "TentacleTip";
        case PartRole::Appendage:   return "Appendage";
        case PartRole::Accessory:   return "Accessory";
        case PartRole::Custom:      return "Custom";
        default:                    return "Unknown";
    }
}

/**
 * @brief Parse a PartRole from string (case-insensitive)
 */
PartRole stringToPartRole(const std::string& str);

/**
 * @brief Extended information about a part's role
 */
struct PartRoleInfo {
    PartRole role = PartRole::Appendage;
    std::string customRoleName;  // For Custom role
    
    /**
     * @brief Check if this role represents a digit (finger, thumb, toe, tentacle tip)
     */
    bool isDigit() const {
        return role == PartRole::Finger || 
               role == PartRole::Thumb || 
               role == PartRole::Toe || 
               role == PartRole::TentacleTip;
    }
    
    /**
     * @brief Check if this role represents a limb segment
     */
    bool isLimbSegment() const {
        return role == PartRole::UpperArm || 
               role == PartRole::LowerArm ||
               role == PartRole::UpperLeg || 
               role == PartRole::LowerLeg;
    }
    
    /**
     * @brief Check if this role represents a spine segment
     */
    bool isSpineSegment() const {
        return role == PartRole::Spine || 
               role == PartRole::Neck || 
               role == PartRole::Tail;
    }
    
    /**
     * @brief Check if this role represents a wing segment
     */
    bool isWingSegment() const {
        return role == PartRole::WingRoot || 
               role == PartRole::WingMid || 
               role == PartRole::WingTip;
    }
    
    /**
     * @brief Check if this role supports surface conform IK (digits)
     */
    bool supportsSurfaceConform() const {
        return isDigit();
    }
    
    /**
     * @brief Check if this role is typically an end effector
     */
    bool isEndEffector() const {
        return role == PartRole::Hand ||
               role == PartRole::Foot ||
               role == PartRole::Head ||
               role == PartRole::TailTip ||
               role == PartRole::WingTip ||
               role == PartRole::TentacleTip ||
               isDigit();
    }
    
    /**
     * @brief Check if this role propagates motion (used for chains like spine/tail)
     */
    bool propagatesMotion() const {
        return isSpineSegment() || 
               role == PartRole::Tentacle;
    }
    
    /**
     * @brief Check if this role supports look-at behavior
     */
    bool supportsLookAt() const {
        return role == PartRole::Head || 
               role == PartRole::Neck ||
               role == PartRole::Eye;
    }
};

/**
 * @brief IK joint type enumeration
 */
enum class JointType : uint32_t {
    Ball,       // Full 3-DOF rotation
    Hinge,      // 1-DOF rotation around axis
    Saddle,     // 2-DOF (like thumb CMC)
    Fixed       // No rotation allowed
};

/**
 * @brief Default constraints for a PartRole
 */
struct PartRoleConstraints {
    PartRole role;
    
    // Default joint limits (can be overridden per-joint)
    glm::vec3 defaultRotationMin{-45.0f, -45.0f, -45.0f};  // Degrees
    glm::vec3 defaultRotationMax{45.0f, 45.0f, 45.0f};    // Degrees
    JointType defaultJointType = JointType::Ball;
    
    // Solver hints
    float defaultStiffness = 0.5f;
    bool preferHinge = false;              // Prefer hinge over ball for this role
    glm::vec3 preferredHingeAxis{1.0f, 0.0f, 0.0f};  // If preferHinge is true
    
    // Behavioral flags
    bool propagatesMotion = false;         // Motion should flow through (spine, tail)
    bool isEndEffector = false;            // Typically targeted by effectors
    bool supportsLookAt = false;           // Can participate in look-at
    bool supportsSurfaceConform = false;   // Can wrap around surfaces
};

/**
 * @brief Library of default constraints per PartRole
 */
class PartRoleConstraintLibrary {
public:
    static const PartRoleConstraintLibrary& instance();
    
    const PartRoleConstraints& getDefaults(PartRole role) const;
    
private:
    PartRoleConstraintLibrary();
    std::map<PartRole, PartRoleConstraints> m_defaults;
};

} // namespace CharacterEditor
