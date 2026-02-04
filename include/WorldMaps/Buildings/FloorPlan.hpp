#pragma once
#include <imgui.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>

// Forward declarations
struct Room;

// ============================================================
// Bezier Curve Helpers
// ============================================================
namespace BezierUtil {
    // Evaluate quadratic bezier (3 control points: start, control, end)
    inline ImVec2 evalQuadratic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, float t) {
        float u = 1.0f - t;
        return ImVec2(
            u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y
        );
    }
    
    // Evaluate cubic bezier (4 control points)
    inline ImVec2 evalCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
        float u = 1.0f - t;
        float u2 = u * u;
        float u3 = u2 * u;
        float t2 = t * t;
        float t3 = t2 * t;
        return ImVec2(
            u3 * p0.x + 3 * u2 * t * p1.x + 3 * u * t2 * p2.x + t3 * p3.x,
            u3 * p0.y + 3 * u2 * t * p1.y + 3 * u * t2 * p2.y + t3 * p3.y
        );
    }
    
    // Sample points along a quadratic bezier curve
    inline std::vector<ImVec2> sampleQuadratic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, int segments = 20) {
        std::vector<ImVec2> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; i++) {
            float t = (float)i / (float)segments;
            points.push_back(evalQuadratic(p0, p1, p2, t));
        }
        return points;
    }
    
    // Sample points along a cubic bezier curve
    inline std::vector<ImVec2> sampleCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, int segments = 20) {
        std::vector<ImVec2> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; i++) {
            float t = (float)i / (float)segments;
            points.push_back(evalCubic(p0, p1, p2, p3, t));
        }
        return points;
    }
    
    // Approximate curve length by sampling
    inline float curveLength(const std::vector<ImVec2>& samples) {
        float len = 0.0f;
        for (size_t i = 1; i < samples.size(); i++) {
            float dx = samples[i].x - samples[i-1].x;
            float dy = samples[i].y - samples[i-1].y;
            len += sqrtf(dx * dx + dy * dy);
        }
        return len;
    }
    
    // Find approximate parameter t for a given arc length
    inline float paramAtLength(const std::vector<ImVec2>& samples, float targetLen) {
        float totalLen = curveLength(samples);
        if (totalLen <= 0) return 0;
        float accum = 0.0f;
        for (size_t i = 1; i < samples.size(); i++) {
            float dx = samples[i].x - samples[i-1].x;
            float dy = samples[i].y - samples[i-1].y;
            float segLen = sqrtf(dx * dx + dy * dy);
            if (accum + segLen >= targetLen) {
                float frac = (targetLen - accum) / segLen;
                return ((float)(i - 1) + frac) / (float)(samples.size() - 1);
            }
            accum += segLen;
        }
        return 1.0f;
    }
    
    // Get tangent at point t on quadratic bezier
    inline ImVec2 tangentQuadratic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, float t) {
        float u = 1.0f - t;
        ImVec2 tan(
            2 * u * (p1.x - p0.x) + 2 * t * (p2.x - p1.x),
            2 * u * (p1.y - p0.y) + 2 * t * (p2.y - p1.y)
        );
        float len = sqrtf(tan.x * tan.x + tan.y * tan.y);
        if (len > 0) { tan.x /= len; tan.y /= len; }
        return tan;
    }
    
    // Get tangent at point t on cubic bezier  
    inline ImVec2 tangentCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
        float u = 1.0f - t;
        float u2 = u * u;
        float t2 = t * t;
        ImVec2 tan(
            3 * u2 * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t2 * (p3.x - p2.x),
            3 * u2 * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t2 * (p3.y - p2.y)
        );
        float len = sqrtf(tan.x * tan.x + tan.y * tan.y);
        if (len > 0) { tan.x /= len; tan.y /= len; }
        return tan;
    }
    
    // Get normal (perpendicular to tangent)
    inline ImVec2 normalFromTangent(const ImVec2& tangent) {
        return ImVec2(-tangent.y, tangent.x);
    }
}

// ============================================================
// Arc Helpers - for circular arc walls
// ============================================================
namespace ArcUtil {
    // Create arc from center, radius, start angle, end angle
    inline std::vector<ImVec2> sampleArc(const ImVec2& center, float radius, 
                                         float startAngle, float endAngle, int segments = 20) {
        std::vector<ImVec2> points;
        points.reserve(segments + 1);
        float angleStep = (endAngle - startAngle) / (float)segments;
        for (int i = 0; i <= segments; i++) {
            float angle = startAngle + i * angleStep;
            points.push_back(ImVec2(
                center.x + radius * cosf(angle),
                center.y + radius * sinf(angle)
            ));
        }
        return points;
    }
    
    // Create arc from 3 points (start, through, end)
    inline bool arcFrom3Points(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3,
                               ImVec2& outCenter, float& outRadius, 
                               float& outStartAngle, float& outEndAngle) {
        // Find circle through 3 points using circumcenter formula
        float ax = p1.x, ay = p1.y;
        float bx = p2.x, by = p2.y;
        float cx = p3.x, cy = p3.y;
        
        float d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (fabsf(d) < 1e-10f) return false; // Points are collinear
        
        float aSq = ax * ax + ay * ay;
        float bSq = bx * bx + by * by;
        float cSq = cx * cx + cy * cy;
        
        outCenter.x = (aSq * (by - cy) + bSq * (cy - ay) + cSq * (ay - by)) / d;
        outCenter.y = (aSq * (cx - bx) + bSq * (ax - cx) + cSq * (bx - ax)) / d;
        
        float dx = ax - outCenter.x;
        float dy = ay - outCenter.y;
        outRadius = sqrtf(dx * dx + dy * dy);
        
        outStartAngle = atan2f(ay - outCenter.y, ax - outCenter.x);
        outEndAngle = atan2f(cy - outCenter.y, cx - outCenter.x);
        
        // Ensure arc goes through p2 by checking direction
        float midAngle = atan2f(by - outCenter.y, bx - outCenter.x);
        float angleDiff = outEndAngle - outStartAngle;
        if (angleDiff < 0) angleDiff += 2 * 3.14159265f;
        float midDiff = midAngle - outStartAngle;
        if (midDiff < 0) midDiff += 2 * 3.14159265f;
        
        if (midDiff > angleDiff) {
            // Need to go the long way around
            std::swap(outStartAngle, outEndAngle);
        }
        
        return true;
    }
}

// ============================================================
// WallType - Type of wall segment
// ============================================================
enum class WallType {
    Straight,       // Simple line from start to end
    QuadraticBezier,// Quadratic curve with 1 control point
    CubicBezier,    // Cubic curve with 2 control points
    Arc             // Circular arc defined by 3 points
};

// ============================================================
// Wall - A line segment or curve with thickness
// ============================================================
struct Wall {
    int id = 0;
    ImVec2 start{0, 0};
    ImVec2 end{0, 0};
    float thickness = 0.15f;  // meters by default
    ImU32 color = IM_COL32(80, 80, 80, 255);
    
    // Curve support
    WallType type = WallType::Straight;
    std::vector<ImVec2> controlPoints;  // For bezier/arc: control points between start and end
    int curveSegments = 20;             // Sampling resolution for curves
    
    // Get sampled points along the wall (for rendering)
    std::vector<ImVec2> getSampledPoints() const {
        std::vector<ImVec2> points;
        switch (type) {
            case WallType::Straight:
                points.push_back(start);
                points.push_back(end);
                break;
            case WallType::QuadraticBezier:
                if (controlPoints.size() >= 1) {
                    points = BezierUtil::sampleQuadratic(start, controlPoints[0], end, curveSegments);
                } else {
                    points.push_back(start);
                    points.push_back(end);
                }
                break;
            case WallType::CubicBezier:
                if (controlPoints.size() >= 2) {
                    points = BezierUtil::sampleCubic(start, controlPoints[0], controlPoints[1], end, curveSegments);
                } else if (controlPoints.size() == 1) {
                    points = BezierUtil::sampleQuadratic(start, controlPoints[0], end, curveSegments);
                } else {
                    points.push_back(start);
                    points.push_back(end);
                }
                break;
            case WallType::Arc:
                if (controlPoints.size() >= 1) {
                    // Control point is the "through" point for arc
                    ImVec2 center;
                    float radius, startAngle, endAngle;
                    if (ArcUtil::arcFrom3Points(start, controlPoints[0], end, center, radius, startAngle, endAngle)) {
                        points = ArcUtil::sampleArc(center, radius, startAngle, endAngle, curveSegments);
                    } else {
                        // Fallback to straight line if collinear
                        points.push_back(start);
                        points.push_back(end);
                    }
                } else {
                    points.push_back(start);
                    points.push_back(end);
                }
                break;
        }
        return points;
    }
    
    // Get wall length (approximated for curves)
    float length() const {
        if (type == WallType::Straight) {
            float dx = end.x - start.x;
            float dy = end.y - start.y;
            return sqrtf(dx * dx + dy * dy);
        }
        return BezierUtil::curveLength(getSampledPoints());
    }
    
    // Get wall angle in radians (for straight walls, or start tangent for curves)
    float angle() const {
        if (type == WallType::Straight) {
            return atan2f(end.y - start.y, end.x - start.x);
        }
        // For curves, return tangent angle at start
        ImVec2 tan = tangentAt(0);
        return atan2f(tan.y, tan.x);
    }
    
    // Get tangent at parameter t (0 to 1)
    ImVec2 tangentAt(float t) const {
        switch (type) {
            case WallType::Straight: {
                float dx = end.x - start.x;
                float dy = end.y - start.y;
                float len = sqrtf(dx * dx + dy * dy);
                return len > 0 ? ImVec2(dx / len, dy / len) : ImVec2(1, 0);
            }
            case WallType::QuadraticBezier:
                if (controlPoints.size() >= 1) {
                    return BezierUtil::tangentQuadratic(start, controlPoints[0], end, t);
                }
                break;
            case WallType::CubicBezier:
                if (controlPoints.size() >= 2) {
                    return BezierUtil::tangentCubic(start, controlPoints[0], controlPoints[1], end, t);
                }
                break;
            case WallType::Arc:
                // For arc, tangent is perpendicular to radius
                if (controlPoints.size() >= 1) {
                    ImVec2 center;
                    float radius, startAngle, endAngle;
                    if (ArcUtil::arcFrom3Points(start, controlPoints[0], end, center, radius, startAngle, endAngle)) {
                        float angle = startAngle + t * (endAngle - startAngle);
                        // Tangent is perpendicular to radius (90 degrees rotated)
                        return ImVec2(-sinf(angle), cosf(angle));
                    }
                }
                break;
        }
        // Default fallback
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float len = sqrtf(dx * dx + dy * dy);
        return len > 0 ? ImVec2(dx / len, dy / len) : ImVec2(1, 0);
    }
    
    // Get normal at parameter t
    ImVec2 normalAt(float t) const {
        ImVec2 tan = tangentAt(t);
        return ImVec2(-tan.y, tan.x);
    }
    
    // Get wall normal (perpendicular) - for backwards compatibility
    ImVec2 normal() const {
        float a = angle();
        return ImVec2(-sinf(a), cosf(a));
    }
    
    // Get point at parameter t (0 to 1)
    ImVec2 pointAt(float t) const {
        switch (type) {
            case WallType::Straight:
                return ImVec2(start.x + t * (end.x - start.x), start.y + t * (end.y - start.y));
            case WallType::QuadraticBezier:
                if (controlPoints.size() >= 1) {
                    return BezierUtil::evalQuadratic(start, controlPoints[0], end, t);
                }
                break;
            case WallType::CubicBezier:
                if (controlPoints.size() >= 2) {
                    return BezierUtil::evalCubic(start, controlPoints[0], controlPoints[1], end, t);
                }
                break;
            case WallType::Arc:
                if (controlPoints.size() >= 1) {
                    ImVec2 center;
                    float radius, startAngle, endAngle;
                    if (ArcUtil::arcFrom3Points(start, controlPoints[0], end, center, radius, startAngle, endAngle)) {
                        float angle = startAngle + t * (endAngle - startAngle);
                        return ImVec2(center.x + radius * cosf(angle), center.y + radius * sinf(angle));
                    }
                }
                break;
        }
        return ImVec2(start.x + t * (end.x - start.x), start.y + t * (end.y - start.y));
    }
    
    // Get wall midpoint
    ImVec2 midpoint() const {
        return pointAt(0.5f);
    }
    
    // Check if a point is near the wall line/curve
    float distanceToPoint(ImVec2 p) const {
        if (type == WallType::Straight) {
            // Original fast path for straight walls
            ImVec2 v = ImVec2(end.x - start.x, end.y - start.y);
            ImVec2 w = ImVec2(p.x - start.x, p.y - start.y);
            float c1 = v.x * w.x + v.y * w.y;
            float c2 = v.x * v.x + v.y * v.y;
            if (c2 == 0) return sqrtf(w.x * w.x + w.y * w.y);
            float t = fmaxf(0.0f, fminf(1.0f, c1 / c2));
            ImVec2 proj = ImVec2(start.x + t * v.x, start.y + t * v.y);
            float dx = p.x - proj.x;
            float dy = p.y - proj.y;
            return sqrtf(dx * dx + dy * dy);
        }
        
        // For curves, sample and find closest point
        auto samples = getSampledPoints();
        float minDist = 1e10f;
        for (size_t i = 0; i < samples.size(); i++) {
            // Check distance to this sample point
            float dx = p.x - samples[i].x;
            float dy = p.y - samples[i].y;
            float dist = sqrtf(dx * dx + dy * dy);
            minDist = fminf(minDist, dist);
            
            // Also check distance to line segment between samples
            if (i > 0) {
                ImVec2 v = ImVec2(samples[i].x - samples[i-1].x, samples[i].y - samples[i-1].y);
                ImVec2 w = ImVec2(p.x - samples[i-1].x, p.y - samples[i-1].y);
                float c1 = v.x * w.x + v.y * w.y;
                float c2 = v.x * v.x + v.y * v.y;
                if (c2 > 0) {
                    float t = fmaxf(0.0f, fminf(1.0f, c1 / c2));
                    ImVec2 proj = ImVec2(samples[i-1].x + t * v.x, samples[i-1].y + t * v.y);
                    dx = p.x - proj.x;
                    dy = p.y - proj.y;
                    dist = sqrtf(dx * dx + dy * dy);
                    minDist = fminf(minDist, dist);
                }
            }
        }
        return minDist;
    }
    
    // Check if this is a curved wall
    bool isCurved() const {
        return type != WallType::Straight;
    }
    
    // Set to quadratic bezier with a single control point
    void setQuadraticBezier(const ImVec2& control) {
        type = WallType::QuadraticBezier;
        controlPoints.clear();
        controlPoints.push_back(control);
    }
    
    // Set to cubic bezier with two control points
    void setCubicBezier(const ImVec2& control1, const ImVec2& control2) {
        type = WallType::CubicBezier;
        controlPoints.clear();
        controlPoints.push_back(control1);
        controlPoints.push_back(control2);
    }
    
    // Set to arc passing through a point
    void setArc(const ImVec2& throughPoint) {
        type = WallType::Arc;
        controlPoints.clear();
        controlPoints.push_back(throughPoint);
    }
    
    // Reset to straight line
    void setStraight() {
        type = WallType::Straight;
        controlPoints.clear();
    }
};

// ============================================================
// Room - A closed polygon area
// ============================================================
struct Room {
    int id = 0;
    std::vector<ImVec2> vertices;
    std::string name = "Room";
    ImU32 fillColor = IM_COL32(200, 200, 220, 100);
    ImU32 outlineColor = IM_COL32(100, 100, 150, 255);
    
    // Check if polygon is closed (first == last or use implicit close)
    bool isClosed() const { return vertices.size() >= 3; }
    
    // Get centroid
    ImVec2 centroid() const {
        if (vertices.empty()) return ImVec2(0, 0);
        float cx = 0, cy = 0;
        for (const auto& v : vertices) {
            cx += v.x;
            cy += v.y;
        }
        return ImVec2(cx / vertices.size(), cy / vertices.size());
    }
    
    // Calculate area (shoelace formula)
    float area() const {
        if (vertices.size() < 3) return 0;
        float a = 0;
        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            a += vertices[i].x * vertices[j].y;
            a -= vertices[j].x * vertices[i].y;
        }
        return fabsf(a) * 0.5f;
    }
    
    // Check if point is inside polygon
    bool containsPoint(ImVec2 p) const {
        if (vertices.size() < 3) return false;
        bool inside = false;
        size_t n = vertices.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            if (((vertices[i].y > p.y) != (vertices[j].y > p.y)) &&
                (p.x < (vertices[j].x - vertices[i].x) * (p.y - vertices[i].y) / 
                       (vertices[j].y - vertices[i].y) + vertices[i].x)) {
                inside = !inside;
            }
        }
        return inside;
    }
};

// ============================================================
// Door - Placed on a wall
// ============================================================
struct Door {
    int id = 0;
    int wallId = -1;           // Which wall this door is on
    float positionOnWall = 0.5f; // 0-1, position along wall
    float width = 0.9f;        // meters
    float rotation = 0;        // swing angle in radians
    bool swingLeft = true;     // swing direction
    ImU32 color = IM_COL32(139, 90, 43, 255);  // brown
};

// ============================================================
// Window - Placed on a wall
// ============================================================
struct Window {
    int id = 0;
    int wallId = -1;
    float positionOnWall = 0.5f;
    float width = 1.2f;        // meters
    float height = 1.0f;       // meters (for display purposes)
    float sillHeight = 0.9f;   // height from floor
    ImU32 color = IM_COL32(135, 206, 235, 200);  // light blue
};

// ============================================================
// Furniture - Placeable objects
// ============================================================
struct Furniture {
    int id = 0;
    int roomId = -1;           // Which room it's in (-1 = none)
    ImVec2 position{0, 0};
    ImVec2 size{1, 1};         // width x depth in meters
    float rotation = 0;        // radians
    std::string type = "generic";
    std::string name = "Furniture";
    ImU32 color = IM_COL32(160, 82, 45, 255);  // sienna
};

// ============================================================
// Staircase - Connects floors
// ============================================================
struct Staircase {
    int id = 0;
    ImVec2 position{0, 0};
    ImVec2 size{1, 3};         // width x length
    float rotation = 0;
    int connectsToFloor = -1;  // Floor index this connects to
    int numSteps = 12;
    ImU32 color = IM_COL32(105, 105, 105, 255);  // dim gray
};

// ============================================================
// FloorPlan - Contains all elements for one floor
// ============================================================
struct FloorPlan {
    std::vector<Wall> walls;
    std::vector<Room> rooms;
    std::vector<Door> doors;
    std::vector<Window> windows;
    std::vector<Furniture> furniture;
    std::vector<Staircase> staircases;
    
    // Grid settings
    float gridSize = 0.5f;     // meters per grid cell
    bool snapToGrid = true;
    std::string units = "m";   // "m", "ft", or custom
    float unitsPerMeter = 1.0f; // for conversion
    
    // ID counters for new elements
    int nextWallId = 1;
    int nextRoomId = 1;
    int nextDoorId = 1;
    int nextWindowId = 1;
    int nextFurnitureId = 1;
    int nextStaircaseId = 1;
    
    // Snap a point to grid if enabled
    ImVec2 snapPoint(ImVec2 p) const {
        if (!snapToGrid || gridSize <= 0) return p;
        return ImVec2(
            roundf(p.x / gridSize) * gridSize,
            roundf(p.y / gridSize) * gridSize
        );
    }
    
    // Add wall and return its ID
    int addWall(ImVec2 start, ImVec2 end, float thickness = 0.15f) {
        Wall w;
        w.id = nextWallId++;
        w.start = snapPoint(start);
        w.end = snapPoint(end);
        w.thickness = thickness;
        walls.push_back(w);
        return w.id;
    }
    
    // Add room and return its ID
    int addRoom(const std::vector<ImVec2>& vertices, const std::string& name = "Room") {
        Room r;
        r.id = nextRoomId++;
        for (const auto& v : vertices) {
            r.vertices.push_back(snapPoint(v));
        }
        r.name = name;
        rooms.push_back(r);
        return r.id;
    }
    
    // Find wall by ID
    Wall* findWall(int id) {
        for (auto& w : walls) if (w.id == id) return &w;
        return nullptr;
    }
    
    // Find room by ID
    Room* findRoom(int id) {
        for (auto& r : rooms) if (r.id == id) return &r;
        return nullptr;
    }
    
    // Find wall at point (within thickness distance)
    Wall* findWallAt(ImVec2 p, float tolerance = 0.1f) {
        for (auto& w : walls) {
            if (w.distanceToPoint(p) < w.thickness * 0.5f + tolerance) {
                return &w;
            }
        }
        return nullptr;
    }
    
    // Find room containing point
    Room* findRoomAt(ImVec2 p) {
        for (auto& r : rooms) {
            if (r.containsPoint(p)) return &r;
        }
        return nullptr;
    }
    
    // Remove wall by ID
    bool removeWall(int id) {
        for (auto it = walls.begin(); it != walls.end(); ++it) {
            if (it->id == id) {
                // Also remove any doors/windows on this wall
                doors.erase(std::remove_if(doors.begin(), doors.end(),
                    [id](const Door& d) { return d.wallId == id; }), doors.end());
                windows.erase(std::remove_if(windows.begin(), windows.end(),
                    [id](const Window& w) { return w.wallId == id; }), windows.end());
                walls.erase(it);
                return true;
            }
        }
        return false;
    }
    
    // Remove room by ID
    bool removeRoom(int id) {
        for (auto it = rooms.begin(); it != rooms.end(); ++it) {
            if (it->id == id) {
                rooms.erase(it);
                return true;
            }
        }
        return false;
    }
    
    // Clear all elements
    void clear() {
        walls.clear();
        rooms.clear();
        doors.clear();
        windows.clear();
        furniture.clear();
        staircases.clear();
        nextWallId = nextRoomId = nextDoorId = nextWindowId = nextFurnitureId = nextStaircaseId = 1;
    }
};
