#include <CharacterEditor/PartRole.hpp>
#include <algorithm>
#include <cctype>

namespace CharacterEditor {

PartRole stringToPartRole(const std::string& str) {
    // Convert to lowercase for comparison
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    
    if (lower == "root") return PartRole::Root;
    if (lower == "spine") return PartRole::Spine;
    if (lower == "chest") return PartRole::Chest;
    if (lower == "neck") return PartRole::Neck;
    if (lower == "head") return PartRole::Head;
    if (lower == "shoulder") return PartRole::Shoulder;
    if (lower == "upperarm" || lower == "upper_arm") return PartRole::UpperArm;
    if (lower == "lowerarm" || lower == "lower_arm" || lower == "forearm") return PartRole::LowerArm;
    if (lower == "hand") return PartRole::Hand;
    if (lower == "finger") return PartRole::Finger;
    if (lower == "thumb") return PartRole::Thumb;
    if (lower == "hip" || lower == "pelvis") return PartRole::Hip;
    if (lower == "upperleg" || lower == "upper_leg" || lower == "thigh") return PartRole::UpperLeg;
    if (lower == "lowerleg" || lower == "lower_leg" || lower == "shin" || lower == "calf") return PartRole::LowerLeg;
    if (lower == "foot") return PartRole::Foot;
    if (lower == "toe") return PartRole::Toe;
    if (lower == "tail") return PartRole::Tail;
    if (lower == "tailtip" || lower == "tail_tip") return PartRole::TailTip;
    if (lower == "wingroot" || lower == "wing_root" || lower == "wing") return PartRole::WingRoot;
    if (lower == "wingmid" || lower == "wing_mid") return PartRole::WingMid;
    if (lower == "wingtip" || lower == "wing_tip") return PartRole::WingTip;
    if (lower == "wingmembrane" || lower == "wing_membrane") return PartRole::WingMembrane;
    if (lower == "ear") return PartRole::Ear;
    if (lower == "jaw") return PartRole::Jaw;
    if (lower == "eye") return PartRole::Eye;
    if (lower == "tentacle") return PartRole::Tentacle;
    if (lower == "tentacletip" || lower == "tentacle_tip") return PartRole::TentacleTip;
    if (lower == "appendage") return PartRole::Appendage;
    if (lower == "accessory") return PartRole::Accessory;
    
    return PartRole::Custom;
}

// PartRoleConstraintLibrary implementation

PartRoleConstraintLibrary::PartRoleConstraintLibrary() {
    // Root
    m_defaults[PartRole::Root] = {
        .role = PartRole::Root,
        .defaultRotationMin = {-10.0f, -180.0f, -10.0f},
        .defaultRotationMax = {10.0f, 180.0f, 10.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.8f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Spine
    m_defaults[PartRole::Spine] = {
        .role = PartRole::Spine,
        .defaultRotationMin = {-30.0f, -45.0f, -20.0f},
        .defaultRotationMax = {45.0f, 45.0f, 20.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.3f,
        .preferHinge = false,
        .propagatesMotion = true,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Chest
    m_defaults[PartRole::Chest] = {
        .role = PartRole::Chest,
        .defaultRotationMin = {-20.0f, -30.0f, -15.0f},
        .defaultRotationMax = {30.0f, 30.0f, 15.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.4f,
        .preferHinge = false,
        .propagatesMotion = true,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Neck
    m_defaults[PartRole::Neck] = {
        .role = PartRole::Neck,
        .defaultRotationMin = {-20.0f, -45.0f, -15.0f},
        .defaultRotationMax = {20.0f, 45.0f, 15.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.2f,
        .preferHinge = false,
        .propagatesMotion = true,
        .isEndEffector = false,
        .supportsLookAt = true,
        .supportsSurfaceConform = false
    };
    
    // Head
    m_defaults[PartRole::Head] = {
        .role = PartRole::Head,
        .defaultRotationMin = {-40.0f, -70.0f, -30.0f},
        .defaultRotationMax = {30.0f, 70.0f, 30.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.3f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = true,
        .supportsSurfaceConform = false
    };
    
    // Shoulder
    m_defaults[PartRole::Shoulder] = {
        .role = PartRole::Shoulder,
        .defaultRotationMin = {-15.0f, -30.0f, -30.0f},
        .defaultRotationMax = {15.0f, 30.0f, 30.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.5f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // UpperArm
    m_defaults[PartRole::UpperArm] = {
        .role = PartRole::UpperArm,
        .defaultRotationMin = {-90.0f, -90.0f, -90.0f},
        .defaultRotationMax = {90.0f, 45.0f, 90.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.4f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // LowerArm
    m_defaults[PartRole::LowerArm] = {
        .role = PartRole::LowerArm,
        .defaultRotationMin = {0.0f, 0.0f, 0.0f},
        .defaultRotationMax = {150.0f, 0.0f, 0.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.5f,
        .preferHinge = true,
        .preferredHingeAxis = {1.0f, 0.0f, 0.0f},
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Hand
    m_defaults[PartRole::Hand] = {
        .role = PartRole::Hand,
        .defaultRotationMin = {-80.0f, -30.0f, -70.0f},
        .defaultRotationMax = {80.0f, 30.0f, 70.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.3f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Finger
    m_defaults[PartRole::Finger] = {
        .role = PartRole::Finger,
        .defaultRotationMin = {-10.0f, -15.0f, 0.0f},
        .defaultRotationMax = {90.0f, 15.0f, 0.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.1f,
        .preferHinge = true,
        .preferredHingeAxis = {1.0f, 0.0f, 0.0f},
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = true
    };
    
    // Thumb
    m_defaults[PartRole::Thumb] = {
        .role = PartRole::Thumb,
        .defaultRotationMin = {-20.0f, -30.0f, -30.0f},
        .defaultRotationMax = {60.0f, 30.0f, 30.0f},
        .defaultJointType = JointType::Saddle,
        .defaultStiffness = 0.15f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = true
    };
    
    // Hip
    m_defaults[PartRole::Hip] = {
        .role = PartRole::Hip,
        .defaultRotationMin = {-10.0f, -30.0f, -10.0f},
        .defaultRotationMax = {10.0f, 30.0f, 10.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.6f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // UpperLeg
    m_defaults[PartRole::UpperLeg] = {
        .role = PartRole::UpperLeg,
        .defaultRotationMin = {-30.0f, -45.0f, -45.0f},
        .defaultRotationMax = {120.0f, 45.0f, 45.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.4f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // LowerLeg
    m_defaults[PartRole::LowerLeg] = {
        .role = PartRole::LowerLeg,
        .defaultRotationMin = {0.0f, 0.0f, 0.0f},
        .defaultRotationMax = {150.0f, 0.0f, 0.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.5f,
        .preferHinge = true,
        .preferredHingeAxis = {1.0f, 0.0f, 0.0f},
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Foot
    m_defaults[PartRole::Foot] = {
        .role = PartRole::Foot,
        .defaultRotationMin = {-45.0f, -30.0f, -30.0f},
        .defaultRotationMax = {45.0f, 30.0f, 30.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.3f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Toe
    m_defaults[PartRole::Toe] = {
        .role = PartRole::Toe,
        .defaultRotationMin = {-30.0f, -10.0f, 0.0f},
        .defaultRotationMax = {60.0f, 10.0f, 0.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.1f,
        .preferHinge = true,
        .preferredHingeAxis = {1.0f, 0.0f, 0.0f},
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = true
    };
    
    // Tail
    m_defaults[PartRole::Tail] = {
        .role = PartRole::Tail,
        .defaultRotationMin = {-30.0f, -45.0f, -30.0f},
        .defaultRotationMax = {30.0f, 45.0f, 30.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.1f,
        .preferHinge = false,
        .propagatesMotion = true,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // TailTip
    m_defaults[PartRole::TailTip] = {
        .role = PartRole::TailTip,
        .defaultRotationMin = {-45.0f, -60.0f, -45.0f},
        .defaultRotationMax = {45.0f, 60.0f, 45.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.05f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // WingRoot
    m_defaults[PartRole::WingRoot] = {
        .role = PartRole::WingRoot,
        .defaultRotationMin = {-30.0f, -10.0f, -90.0f},
        .defaultRotationMax = {120.0f, 45.0f, 30.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.4f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // WingMid
    m_defaults[PartRole::WingMid] = {
        .role = PartRole::WingMid,
        .defaultRotationMin = {0.0f, -10.0f, -60.0f},
        .defaultRotationMax = {150.0f, 10.0f, 30.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.3f,
        .preferHinge = true,
        .preferredHingeAxis = {1.0f, 0.0f, 0.0f},
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // WingTip
    m_defaults[PartRole::WingTip] = {
        .role = PartRole::WingTip,
        .defaultRotationMin = {0.0f, 0.0f, -10.0f},
        .defaultRotationMax = {0.0f, 0.0f, 90.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.2f,
        .preferHinge = true,
        .preferredHingeAxis = {0.0f, 0.0f, 1.0f},
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // WingMembrane
    m_defaults[PartRole::WingMembrane] = {
        .role = PartRole::WingMembrane,
        .defaultRotationMin = {0.0f, 0.0f, 0.0f},
        .defaultRotationMax = {0.0f, 0.0f, 0.0f},
        .defaultJointType = JointType::Fixed,
        .defaultStiffness = 1.0f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Ear
    m_defaults[PartRole::Ear] = {
        .role = PartRole::Ear,
        .defaultRotationMin = {-30.0f, -45.0f, -20.0f},
        .defaultRotationMax = {30.0f, 45.0f, 20.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.2f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Jaw
    m_defaults[PartRole::Jaw] = {
        .role = PartRole::Jaw,
        .defaultRotationMin = {0.0f, -5.0f, -5.0f},
        .defaultRotationMax = {30.0f, 5.0f, 5.0f},
        .defaultJointType = JointType::Hinge,
        .defaultStiffness = 0.3f,
        .preferHinge = true,
        .preferredHingeAxis = {1.0f, 0.0f, 0.0f},
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Eye
    m_defaults[PartRole::Eye] = {
        .role = PartRole::Eye,
        .defaultRotationMin = {-30.0f, -45.0f, 0.0f},
        .defaultRotationMax = {30.0f, 45.0f, 0.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.1f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = true,
        .supportsSurfaceConform = false
    };
    
    // Tentacle
    m_defaults[PartRole::Tentacle] = {
        .role = PartRole::Tentacle,
        .defaultRotationMin = {-60.0f, -60.0f, -60.0f},
        .defaultRotationMax = {60.0f, 60.0f, 60.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.05f,
        .preferHinge = false,
        .propagatesMotion = true,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // TentacleTip
    m_defaults[PartRole::TentacleTip] = {
        .role = PartRole::TentacleTip,
        .defaultRotationMin = {-90.0f, -90.0f, -90.0f},
        .defaultRotationMax = {90.0f, 90.0f, 90.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.02f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = true,
        .supportsLookAt = false,
        .supportsSurfaceConform = true
    };
    
    // Appendage (generic)
    m_defaults[PartRole::Appendage] = {
        .role = PartRole::Appendage,
        .defaultRotationMin = {-45.0f, -45.0f, -45.0f},
        .defaultRotationMax = {45.0f, 45.0f, 45.0f},
        .defaultJointType = JointType::Ball,
        .defaultStiffness = 0.5f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Accessory
    m_defaults[PartRole::Accessory] = {
        .role = PartRole::Accessory,
        .defaultRotationMin = {0.0f, 0.0f, 0.0f},
        .defaultRotationMax = {0.0f, 0.0f, 0.0f},
        .defaultJointType = JointType::Fixed,
        .defaultStiffness = 1.0f,
        .preferHinge = false,
        .propagatesMotion = false,
        .isEndEffector = false,
        .supportsLookAt = false,
        .supportsSurfaceConform = false
    };
    
    // Custom (same as Appendage)
    m_defaults[PartRole::Custom] = m_defaults[PartRole::Appendage];
    m_defaults[PartRole::Custom].role = PartRole::Custom;
}

const PartRoleConstraintLibrary& PartRoleConstraintLibrary::instance() {
    static PartRoleConstraintLibrary lib;
    return lib;
}

const PartRoleConstraints& PartRoleConstraintLibrary::getDefaults(PartRole role) const {
    auto it = m_defaults.find(role);
    if (it != m_defaults.end()) {
        return it->second;
    }
    // Fallback to Appendage
    return m_defaults.at(PartRole::Appendage);
}

} // namespace CharacterEditor
