#pragma once
#include <imgui.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <set>

// Forward declarations
struct Room;

// ============================================================
// B-Spline Helpers - Uniform cubic B-spline evaluation
// ============================================================
namespace BSplineUtil {
    // Evaluate uniform cubic B-spline basis functions at parameter t [0,1]
    inline void basisFunctions(float t, float& b0, float& b1, float& b2, float& b3) {
        float t2 = t * t;
        float t3 = t2 * t;
        float omt = 1.0f - t;
        float omt2 = omt * omt;
        float omt3 = omt2 * omt;
        
        // Uniform cubic B-spline basis (Cox-de Boor)
        b0 = omt3 / 6.0f;
        b1 = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
        b2 = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
        b3 = t3 / 6.0f;
    }
    
    // Evaluate uniform cubic B-spline at parameter t using 4 control points
    inline ImVec2 evalCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
        float b0, b1, b2, b3;
        basisFunctions(t, b0, b1, b2, b3);
        return ImVec2(
            b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
            b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y
        );
    }
    
    // Evaluate B-spline tangent at parameter t
    inline ImVec2 tangentCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
        float t2 = t * t;
        // Derivatives of basis functions
        float db0 = -0.5f * (1.0f - t) * (1.0f - t);
        float db1 = 0.5f * (3.0f * t2 - 4.0f * t);
        float db2 = 0.5f * (-3.0f * t2 + 2.0f * t + 1.0f);
        float db3 = 0.5f * t2;
        
        ImVec2 tan(
            db0 * p0.x + db1 * p1.x + db2 * p2.x + db3 * p3.x,
            db0 * p0.y + db1 * p1.y + db2 * p2.y + db3 * p3.y
        );
        float len = sqrtf(tan.x * tan.x + tan.y * tan.y);
        if (len > 0) { tan.x /= len; tan.y /= len; }
        return tan;
    }
    
    // Sample a complete B-spline curve from a control point array
    // For n control points, generates (n-3) segments
    inline std::vector<ImVec2> sampleBSpline(const std::vector<ImVec2>& controlPoints, int segmentsPerSpan = 10, bool closed = false) {
        std::vector<ImVec2> points;
        if (controlPoints.size() < 4) {
            // Not enough points for cubic B-spline, return control points as line
            return controlPoints;
        }
        
        size_t n = controlPoints.size();
        size_t numSpans = closed ? n : (n - 3);
        
        for (size_t span = 0; span < numSpans; span++) {
            size_t i0 = span % n;
            size_t i1 = (span + 1) % n;
            size_t i2 = (span + 2) % n;
            size_t i3 = (span + 3) % n;
            
            for (int seg = 0; seg < segmentsPerSpan; seg++) {
                float t = (float)seg / (float)segmentsPerSpan;
                points.push_back(evalCubic(controlPoints[i0], controlPoints[i1], 
                                          controlPoints[i2], controlPoints[i3], t));
            }
        }
        
        // Add final point
        if (!closed && n >= 4) {
            points.push_back(evalCubic(controlPoints[n-4], controlPoints[n-3], 
                                      controlPoints[n-2], controlPoints[n-1], 1.0f));
        } else if (closed) {
            // Close the loop
            points.push_back(points[0]);
        }
        
        return points;
    }
    
    // Sample a Bezier spline (connected cubic Bezier curves)
    // Control points: P0, C0a, C0b, P1, C1a, C1b, P2, ... (3 points per segment + 1 start)
    // Or for simpler use: just pass all control points and it creates smooth connections
    inline std::vector<ImVec2> sampleBezierSpline(const std::vector<ImVec2>& controlPoints, int segmentsPerCurve = 20, bool closed = false) {
        std::vector<ImVec2> points;
        if (controlPoints.size() < 2) return controlPoints;
        
        if (controlPoints.size() == 2) {
            // Just a line
            points.push_back(controlPoints[0]);
            points.push_back(controlPoints[1]);
            return points;
        }
        
        // Treat as Catmull-Rom style: auto-generate tangents for smooth curves
        size_t n = controlPoints.size();
        for (size_t i = 0; i < n - 1; i++) {
            const ImVec2& p0 = controlPoints[i];
            const ImVec2& p1 = controlPoints[i + 1];
            
            // Calculate tangent handles (Catmull-Rom style)
            ImVec2 m0, m1;
            if (i == 0) {
                if (closed) {
                    m0 = ImVec2((p1.x - controlPoints[n-1].x) * 0.5f, (p1.y - controlPoints[n-1].y) * 0.5f);
                } else {
                    m0 = ImVec2((p1.x - p0.x) * 0.5f, (p1.y - p0.y) * 0.5f);
                }
            } else {
                m0 = ImVec2((p1.x - controlPoints[i-1].x) * 0.5f, (p1.y - controlPoints[i-1].y) * 0.5f);
            }
            
            if (i == n - 2) {
                if (closed) {
                    m1 = ImVec2((controlPoints[0].x - p0.x) * 0.5f, (controlPoints[0].y - p0.y) * 0.5f);
                } else {
                    m1 = ImVec2((p1.x - p0.x) * 0.5f, (p1.y - p0.y) * 0.5f);
                }
            } else {
                m1 = ImVec2((controlPoints[i+2].x - p0.x) * 0.5f, (controlPoints[i+2].y - p0.y) * 0.5f);
            }
            
            // Convert to cubic Bezier control points
            ImVec2 c0 = ImVec2(p0.x + m0.x / 3.0f, p0.y + m0.y / 3.0f);
            ImVec2 c1 = ImVec2(p1.x - m1.x / 3.0f, p1.y - m1.y / 3.0f);
            
            // Sample this segment
            for (int seg = 0; seg <= segmentsPerCurve; seg++) {
                float t = (float)seg / (float)segmentsPerCurve;
                float u = 1.0f - t;
                float u2 = u * u;
                float u3 = u2 * u;
                float t2 = t * t;
                float t3 = t2 * t;
                
                points.push_back(ImVec2(
                    u3 * p0.x + 3 * u2 * t * c0.x + 3 * u * t2 * c1.x + t3 * p1.x,
                    u3 * p0.y + 3 * u2 * t * c0.y + 3 * u * t2 * c1.y + t3 * p1.y
                ));
            }
        }
        
        // Close the loop if needed
        if (closed && n >= 2) {
            const ImVec2& p0 = controlPoints[n-1];
            const ImVec2& p1 = controlPoints[0];
            ImVec2 m0 = ImVec2((p1.x - controlPoints[n-2].x) * 0.5f, (p1.y - controlPoints[n-2].y) * 0.5f);
            ImVec2 m1 = ImVec2((controlPoints[1].x - p0.x) * 0.5f, (controlPoints[1].y - p0.y) * 0.5f);
            ImVec2 c0 = ImVec2(p0.x + m0.x / 3.0f, p0.y + m0.y / 3.0f);
            ImVec2 c1 = ImVec2(p1.x - m1.x / 3.0f, p1.y - m1.y / 3.0f);
            
            for (int seg = 0; seg <= segmentsPerCurve; seg++) {
                float t = (float)seg / (float)segmentsPerCurve;
                float u = 1.0f - t;
                float u2 = u * u;
                float u3 = u2 * u;
                float t2 = t * t;
                float t3 = t2 * t;
                
                points.push_back(ImVec2(
                    u3 * p0.x + 3 * u2 * t * c0.x + 3 * u * t2 * c1.x + t3 * p1.x,
                    u3 * p0.y + 3 * u2 * t * c0.y + 3 * u * t2 * c1.y + t3 * p1.y
                ));
            }
        }
        
        return points;
    }
}

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
    Arc,            // Circular arc defined by 3 points
    BSpline,        // Uniform cubic B-spline (4+ control points)
    BezierSpline    // Connected Bezier curves (Catmull-Rom style, 2+ points)
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
    bool isClosed = false;              // For splines: connect end to start
    
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
            case WallType::BSpline:
                if (controlPoints.size() >= 2) {
                    // Build full control point list: start + controlPoints (+ end if not closed)
                    // For closed splines, end == start, so don't add duplicate
                    std::vector<ImVec2> allPts;
                    allPts.push_back(start);
                    for (const auto& cp : controlPoints) allPts.push_back(cp);
                    if (!isClosed) allPts.push_back(end);
                    points = BSplineUtil::sampleBSpline(allPts, curveSegments / 2, isClosed);
                } else {
                    points.push_back(start);
                    for (const auto& cp : controlPoints) points.push_back(cp);
                    points.push_back(end);
                }
                break;
            case WallType::BezierSpline:
                {
                    // Build full control point list: start + controlPoints (+ end if not closed)
                    // For closed splines, end == start, so don't add duplicate
                    std::vector<ImVec2> allPts;
                    allPts.push_back(start);
                    for (const auto& cp : controlPoints) allPts.push_back(cp);
                    if (!isClosed) allPts.push_back(end);
                    points = BSplineUtil::sampleBezierSpline(allPts, curveSegments, isClosed);
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
            case WallType::BSpline:
            case WallType::BezierSpline:
                {
                    // For splines, approximate tangent from sampled points
                    auto samples = getSampledPoints();
                    if (samples.size() < 2) break;
                    
                    // Find position in samples at parameter t
                    float totalLen = BezierUtil::curveLength(samples);
                    float targetLen = t * totalLen;
                    float accum = 0.0f;
                    
                    for (size_t i = 1; i < samples.size(); i++) {
                        float dx = samples[i].x - samples[i-1].x;
                        float dy = samples[i].y - samples[i-1].y;
                        float segLen = sqrtf(dx * dx + dy * dy);
                        if (accum + segLen >= targetLen || i == samples.size() - 1) {
                            // Return normalized direction of this segment
                            float len = sqrtf(dx * dx + dy * dy);
                            if (len > 0) {
                                return ImVec2(dx / len, dy / len);
                            }
                            break;
                        }
                        accum += segLen;
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
            case WallType::BSpline:
            case WallType::BezierSpline:
                {
                    // For splines, sample and interpolate along sampled points
                    auto samples = getSampledPoints();
                    if (samples.empty()) break;
                    if (samples.size() == 1) return samples[0];
                    
                    // Compute total length and find position at t
                    float totalLen = BezierUtil::curveLength(samples);
                    float targetLen = t * totalLen;
                    float accum = 0.0f;
                    
                    for (size_t i = 1; i < samples.size(); i++) {
                        float dx = samples[i].x - samples[i-1].x;
                        float dy = samples[i].y - samples[i-1].y;
                        float segLen = sqrtf(dx * dx + dy * dy);
                        if (accum + segLen >= targetLen && segLen > 0) {
                            float frac = (targetLen - accum) / segLen;
                            return ImVec2(
                                samples[i-1].x + frac * dx,
                                samples[i-1].y + frac * dy
                            );
                        }
                        accum += segLen;
                    }
                    return samples.back();
                }
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
        isClosed = false;
    }
    
    // Set to B-spline with multiple control points
    void setBSpline(const std::vector<ImVec2>& points, bool closed = false) {
        type = WallType::BSpline;
        controlPoints = points;
        isClosed = closed;
    }
    
    // Set to Bezier spline (Catmull-Rom style) with multiple control points
    void setBezierSpline(const std::vector<ImVec2>& points, bool closed = false) {
        type = WallType::BezierSpline;
        controlPoints = points;
        isClosed = closed;
    }
    
    // Find closest point on wall to given position
    // Returns: parameter t (0-1), and sets outDistance to the distance
    float closestPointTo(ImVec2 p, float* outDistance = nullptr) const {
        if (type == WallType::Straight) {
            // Fast path for straight walls
            ImVec2 v = ImVec2(end.x - start.x, end.y - start.y);
            ImVec2 w = ImVec2(p.x - start.x, p.y - start.y);
            float c1 = v.x * w.x + v.y * w.y;
            float c2 = v.x * v.x + v.y * v.y;
            if (c2 == 0) {
                if (outDistance) *outDistance = sqrtf(w.x * w.x + w.y * w.y);
                return 0.0f;
            }
            float t = fmaxf(0.0f, fminf(1.0f, c1 / c2));
            if (outDistance) {
                ImVec2 proj = ImVec2(start.x + t * v.x, start.y + t * v.y);
                float dx = p.x - proj.x;
                float dy = p.y - proj.y;
                *outDistance = sqrtf(dx * dx + dy * dy);
            }
            return t;
        }
        
        // For curves, sample and find closest point
        auto samples = getSampledPoints();
        if (samples.empty()) {
            if (outDistance) *outDistance = 1e10f;
            return 0.0f;
        }
        
        float totalLen = BezierUtil::curveLength(samples);
        float minDist = 1e10f;
        float bestT = 0.0f;
        float accumLen = 0.0f;
        
        for (size_t i = 0; i < samples.size(); i++) {
            // Check distance to this sample point
            float dx = p.x - samples[i].x;
            float dy = p.y - samples[i].y;
            float dist = sqrtf(dx * dx + dy * dy);
            
            float currentT = (totalLen > 0) ? (accumLen / totalLen) : 0.0f;
            
            if (dist < minDist) {
                minDist = dist;
                bestT = currentT;
            }
            
            // Also check distance to line segment between samples
            if (i > 0) {
                ImVec2 v = ImVec2(samples[i].x - samples[i-1].x, samples[i].y - samples[i-1].y);
                ImVec2 w = ImVec2(p.x - samples[i-1].x, p.y - samples[i-1].y);
                float c1 = v.x * w.x + v.y * w.y;
                float c2 = v.x * v.x + v.y * v.y;
                if (c2 > 0) {
                    float segT = fmaxf(0.0f, fminf(1.0f, c1 / c2));
                    ImVec2 proj = ImVec2(samples[i-1].x + segT * v.x, samples[i-1].y + segT * v.y);
                    dx = p.x - proj.x;
                    dy = p.y - proj.y;
                    dist = sqrtf(dx * dx + dy * dy);
                    if (dist < minDist) {
                        minDist = dist;
                        float segLen = sqrtf(c2);
                        float prevLen = accumLen - segLen;
                        bestT = (totalLen > 0) ? ((prevLen + segT * segLen) / totalLen) : 0.0f;
                    }
                }
            }
            
            // Update accumulated length
            if (i > 0) {
                float dx2 = samples[i].x - samples[i-1].x;
                float dy2 = samples[i].y - samples[i-1].y;
                accumLen += sqrtf(dx2 * dx2 + dy2 * dy2);
            }
        }
        
        if (outDistance) *outDistance = minDist;
        return fmaxf(0.0f, fminf(1.0f, bestT));
    }
};

// ============================================================
// RoomEdge - A single edge of a room boundary (supports curves)
// ============================================================
enum class RoomEdgeType {
    Straight,       // Simple line between vertices
    QuadraticBezier,// Quadratic curve with 1 control point
    CubicBezier,    // Cubic curve with 2 control points
    Arc,            // Circular arc
    FromWall        // Edge derived from a wall (references wallId)
};

struct RoomEdge {
    RoomEdgeType type = RoomEdgeType::Straight;
    std::vector<ImVec2> controlPoints;  // Control points for curves
    int wallId = -1;                     // If FromWall, references the wall
    bool reversed = false;               // If true, wall is traversed end->start
    
    // Get sampled points for this edge (given start and end vertices)
    std::vector<ImVec2> getSampledPoints(const ImVec2& start, const ImVec2& end, int segments = 20) const {
        std::vector<ImVec2> points;
        switch (type) {
            case RoomEdgeType::Straight:
                points.push_back(start);
                points.push_back(end);
                break;
            case RoomEdgeType::QuadraticBezier:
                if (controlPoints.size() >= 1) {
                    points = BezierUtil::sampleQuadratic(start, controlPoints[0], end, segments);
                } else {
                    points.push_back(start);
                    points.push_back(end);
                }
                break;
            case RoomEdgeType::CubicBezier:
                if (controlPoints.size() >= 2) {
                    points = BezierUtil::sampleCubic(start, controlPoints[0], controlPoints[1], end, segments);
                } else {
                    points.push_back(start);
                    points.push_back(end);
                }
                break;
            case RoomEdgeType::Arc:
                if (controlPoints.size() >= 1) {
                    ImVec2 center;
                    float radius, startAngle, endAngle;
                    if (ArcUtil::arcFrom3Points(start, controlPoints[0], end, center, radius, startAngle, endAngle)) {
                        points = ArcUtil::sampleArc(center, radius, startAngle, endAngle, segments);
                    } else {
                        points.push_back(start);
                        points.push_back(end);
                    }
                } else {
                    points.push_back(start);
                    points.push_back(end);
                }
                break;
            case RoomEdgeType::FromWall:
                // This will be filled in by FloorPlan using the actual wall geometry
                points.push_back(start);
                points.push_back(end);
                break;
        }
        return points;
    }
};

// ============================================================
// Room - A closed polygon area with optional curved edges
// ============================================================
struct Room {
    int id = 0;
    std::vector<ImVec2> vertices;       // Corner vertices
    std::vector<RoomEdge> edges;        // Edge definitions (optional - if empty, all straight)
    std::vector<int> wallIds;           // Optional: wall IDs that form this room boundary
    std::string name = "Room";
    ImU32 fillColor = IM_COL32(200, 200, 220, 100);
    ImU32 outlineColor = IM_COL32(100, 100, 150, 255);
    
    // Check if polygon is closed (first == last or use implicit close)
    bool isClosed() const { return vertices.size() >= 3; }
    
    // Check if this room has curved edges
    bool hasCurvedEdges() const {
        for (const auto& edge : edges) {
            if (edge.type != RoomEdgeType::Straight) return true;
        }
        return false;
    }
    
    // Get all sampled points for the room boundary (for rendering)
    std::vector<ImVec2> getSampledBoundary(int segmentsPerEdge = 20) const {
        std::vector<ImVec2> points;
        if (vertices.size() < 3) return points;
        
        size_t n = vertices.size();
        for (size_t i = 0; i < n; i++) {
            const ImVec2& start = vertices[i];
            const ImVec2& end = vertices[(i + 1) % n];
            
            // Get edge samples
            std::vector<ImVec2> edgePts;
            if (i < edges.size() && edges[i].type != RoomEdgeType::Straight) {
                edgePts = edges[i].getSampledPoints(start, end, segmentsPerEdge);
            } else {
                edgePts.push_back(start);
                edgePts.push_back(end);
            }
            
            // Add points (except last to avoid duplicates)
            for (size_t j = 0; j < edgePts.size() - 1; j++) {
                points.push_back(edgePts[j]);
            }
        }
        
        // Close the loop
        if (!points.empty()) {
            points.push_back(points[0]);
        }
        
        return points;
    }
    
    // Get centroid (uses vertices only for simplicity)
    ImVec2 centroid() const {
        if (vertices.empty()) return ImVec2(0, 0);
        float cx = 0, cy = 0;
        for (const auto& v : vertices) {
            cx += v.x;
            cy += v.y;
        }
        return ImVec2(cx / vertices.size(), cy / vertices.size());
    }
    
    // Calculate approximate area using sampled boundary
    float area() const {
        auto boundary = getSampledBoundary(10);
        if (boundary.size() < 3) return 0;
        float a = 0;
        for (size_t i = 0; i < boundary.size() - 1; i++) {
            size_t j = (i + 1) % (boundary.size() - 1);
            a += boundary[i].x * boundary[j].y;
            a -= boundary[j].x * boundary[i].y;
        }
        return fabsf(a) * 0.5f;
    }
    
    // Check if point is inside room (uses sampled boundary for curves)
    bool containsPoint(ImVec2 p) const {
        auto boundary = getSampledBoundary(10);
        if (boundary.size() < 3) return false;
        
        bool inside = false;
        size_t n = boundary.size() - 1; // Exclude duplicate closing point
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            if (((boundary[i].y > p.y) != (boundary[j].y > p.y)) &&
                (p.x < (boundary[j].x - boundary[i].x) * (p.y - boundary[i].y) / 
                       (boundary[j].y - boundary[i].y) + boundary[i].x)) {
                inside = !inside;
            }
        }
        return inside;
    }
    
    // Set edge type for a specific edge
    void setEdgeType(size_t edgeIndex, RoomEdgeType type, const std::vector<ImVec2>& controlPts = {}) {
        // Ensure edges vector is large enough
        while (edges.size() <= edgeIndex) {
            edges.push_back(RoomEdge{});
        }
        edges[edgeIndex].type = type;
        edges[edgeIndex].controlPoints = controlPts;
    }
    
    // Set edge as arc through a point
    void setEdgeArc(size_t edgeIndex, const ImVec2& throughPoint) {
        setEdgeType(edgeIndex, RoomEdgeType::Arc, {throughPoint});
    }
    
    // Set edge as quadratic bezier
    void setEdgeQuadratic(size_t edgeIndex, const ImVec2& controlPoint) {
        setEdgeType(edgeIndex, RoomEdgeType::QuadraticBezier, {controlPoint});
    }
    
    // Set edge as cubic bezier
    void setEdgeCubic(size_t edgeIndex, const ImVec2& cp1, const ImVec2& cp2) {
        setEdgeType(edgeIndex, RoomEdgeType::CubicBezier, {cp1, cp2});
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
    std::string texture = ""; // texture/icon name (empty = solid color)
    ImU32 color = IM_COL32(160, 82, 45, 255);  // sienna
    
    // Get rotated corner points (for rendering)
    void getCorners(ImVec2 outCorners[4]) const {
        float c = cosf(rotation);
        float s = sinf(rotation);
        float hw = size.x * 0.5f;
        float hd = size.y * 0.5f;
        // Local corners: TL, TR, BR, BL
        ImVec2 local[4] = {
            ImVec2(-hw, -hd), ImVec2(hw, -hd),
            ImVec2(hw, hd), ImVec2(-hw, hd)
        };
        for (int i = 0; i < 4; i++) {
            outCorners[i] = ImVec2(
                position.x + local[i].x * c - local[i].y * s,
                position.y + local[i].x * s + local[i].y * c
            );
        }
    }
};

// ============================================================
// Staircase - Connects floors
// ============================================================
struct Staircase {
    int id = 0;
    ImVec2 position{0, 0};
    ImVec2 size{1, 3};         // width x length
    float rotation = 0;        // radians
    int connectsToFloor = -1;  // Floor index this connects to
    int numSteps = 12;
    ImU32 color = IM_COL32(105, 105, 105, 255);  // dim gray
    
    // For curved staircases following a wall
    int wallId = -1;           // Wall to follow (-1 = straight staircase)
    float startOnWall = 0.0f;  // Start position on wall (0-1)
    float endOnWall = 1.0f;    // End position on wall (0-1)
    
    // Get rotated corner points (for rendering straight stairs)
    void getCorners(ImVec2 outCorners[4]) const {
        float c = cosf(rotation);
        float s = sinf(rotation);
        float hw = size.x * 0.5f;
        float hl = size.y * 0.5f;
        // Local corners: TL, TR, BR, BL
        ImVec2 local[4] = {
            ImVec2(-hw, -hl), ImVec2(hw, -hl),
            ImVec2(hw, hl), ImVec2(-hw, hl)
        };
        for (int i = 0; i < 4; i++) {
            outCorners[i] = ImVec2(
                position.x + local[i].x * c - local[i].y * s,
                position.y + local[i].x * s + local[i].y * c
            );
        }
    }
    
    // Check if this is a curved staircase
    bool isCurved() const { return wallId >= 0; }
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
    
    // ================================================================
    // Wall Loop Detection - Find enclosed polygon from walls at point
    // ================================================================
    
    // Helper: Check if two points are close (within tolerance)
    static bool pointsClose(const ImVec2& a, const ImVec2& b, float tol = 0.05f) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return (dx * dx + dy * dy) < tol * tol;
    }
    
    // Helper: Get wall endpoint (0=start, 1=end)
    static ImVec2 wallEndpoint(const Wall& w, int which) {
        return which == 0 ? w.start : w.end;
    }
    
    // Find walls connected to a given point
    std::vector<std::pair<int, int>> findWallsAtPoint(const ImVec2& pt, float tolerance = 0.1f) const {
        std::vector<std::pair<int, int>> result; // {wallIndex, endpoint: 0=start, 1=end}
        for (size_t i = 0; i < walls.size(); i++) {
            if (pointsClose(walls[i].start, pt, tolerance)) {
                result.push_back({static_cast<int>(i), 0});
            }
            if (pointsClose(walls[i].end, pt, tolerance)) {
                result.push_back({static_cast<int>(i), 1});
            }
        }
        return result;
    }
    
    // Create room from enclosed wall loop at a clicked point
    // Returns room ID if successful, -1 if no enclosed loop found
    int createRoomFromWallsAt(const ImVec2& clickPoint, const std::string& roomName = "Room") {
        // Find the smallest enclosing polygon of walls that contains clickPoint
        // Using a left-hand wall-following algorithm
        
        if (walls.empty()) return -1;
        
        // First, find the wall closest to the click point
        int startWallIdx = -1;
        float minDist = 1e10f;
        for (size_t i = 0; i < walls.size(); i++) {
            float dist = walls[i].distanceToPoint(clickPoint);
            if (dist < minDist) {
                minDist = dist;
                startWallIdx = static_cast<int>(i);
            }
        }
        
        if (startWallIdx < 0) return -1;
        
        // Try to find a closed loop starting from this wall
        // We'll use a graph-based approach
        
        const float CONNECT_TOL = 0.15f; // Connection tolerance
        
        // Build adjacency: for each wall endpoint, find connected walls
        struct WallVisit {
            int wallIdx;
            bool reversed;  // true if traversing end->start
        };
        
        // Try starting from both endpoints
        for (int startEndpoint = 0; startEndpoint < 2; startEndpoint++) {
            std::vector<WallVisit> path;
            std::set<int> visitedWalls;
            
            WallVisit current;
            current.wallIdx = startWallIdx;
            current.reversed = (startEndpoint == 1);
            
            ImVec2 startPt = current.reversed ? walls[startWallIdx].end : walls[startWallIdx].start;
            ImVec2 currentPos = current.reversed ? walls[startWallIdx].start : walls[startWallIdx].end;
            
            path.push_back(current);
            visitedWalls.insert(startWallIdx);
            
            // Follow walls using left-hand rule
            bool foundLoop = false;
            int maxIter = static_cast<int>(walls.size()) * 2; // Safety limit
            
            for (int iter = 0; iter < maxIter && !foundLoop; iter++) {
                // Check if we've returned to start
                if (path.size() >= 3 && pointsClose(currentPos, startPt, CONNECT_TOL)) {
                    foundLoop = true;
                    break;
                }
                
                // Find all walls connected at currentPos
                auto connected = findWallsAtPoint(currentPos, CONNECT_TOL);
                if (connected.empty()) break;
                
                // Get incoming direction
                const Wall& prevWall = walls[current.wallIdx];
                ImVec2 inDir;
                if (current.reversed) {
                    inDir = ImVec2(prevWall.start.x - prevWall.end.x, prevWall.start.y - prevWall.end.y);
                } else {
                    inDir = ImVec2(prevWall.end.x - prevWall.start.x, prevWall.end.y - prevWall.start.y);
                }
                float inLen = sqrtf(inDir.x * inDir.x + inDir.y * inDir.y);
                if (inLen > 0) { inDir.x /= inLen; inDir.y /= inLen; }
                
                // Find the wall with the smallest left turn (counterclockwise)
                int bestWallIdx = -1;
                int bestEndpoint = 0;
                float bestAngle = 1e10f;
                
                for (const auto& conn : connected) {
                    if (conn.first == current.wallIdx) continue; // Skip current wall
                    
                    const Wall& nextWall = walls[conn.first];
                    
                    // Determine outgoing direction
                    ImVec2 outDir;
                    bool wouldReverse;
                    if (conn.second == 0) {
                        // Connected at start, would traverse start->end
                        outDir = ImVec2(nextWall.end.x - nextWall.start.x, nextWall.end.y - nextWall.start.y);
                        wouldReverse = false;
                    } else {
                        // Connected at end, would traverse end->start
                        outDir = ImVec2(nextWall.start.x - nextWall.end.x, nextWall.start.y - nextWall.end.y);
                        wouldReverse = true;
                    }
                    
                    float outLen = sqrtf(outDir.x * outDir.x + outDir.y * outDir.y);
                    if (outLen > 0) { outDir.x /= outLen; outDir.y /= outLen; }
                    
                    // Calculate signed angle from incoming to outgoing (positive = left turn)
                    // cross = inDir.x * outDir.y - inDir.y * outDir.x
                    // dot = inDir.x * outDir.x + inDir.y * outDir.y
                    float cross = inDir.x * outDir.y - inDir.y * outDir.x;
                    float dot = inDir.x * outDir.x + inDir.y * outDir.y;
                    float angle = atan2f(cross, dot);
                    
                    // We want the rightmost turn (smallest angle, most negative cross product)
                    // For a counterclockwise room boundary
                    if (visitedWalls.find(conn.first) == visitedWalls.end() || 
                        (pointsClose(wouldReverse ? nextWall.start : nextWall.end, startPt, CONNECT_TOL) && path.size() >= 2)) {
                        if (angle < bestAngle) {
                            bestAngle = angle;
                            bestWallIdx = conn.first;
                            bestEndpoint = conn.second;
                        }
                    }
                }
                
                if (bestWallIdx < 0) break; // Dead end
                
                // Move to next wall
                current.wallIdx = bestWallIdx;
                current.reversed = (bestEndpoint == 1);
                currentPos = current.reversed ? walls[bestWallIdx].start : walls[bestWallIdx].end;
                
                if (visitedWalls.find(bestWallIdx) == visitedWalls.end()) {
                    visitedWalls.insert(bestWallIdx);
                    path.push_back(current);
                } else if (pointsClose(currentPos, startPt, CONNECT_TOL)) {
                    // We've completed the loop
                    foundLoop = true;
                } else {
                    break; // Revisiting a wall without closing - abort
                }
            }
            
            if (foundLoop && path.size() >= 3) {
                // Build room from the path
                Room room;
                room.id = nextRoomId++;
                room.name = roomName;
                
                // Collect vertices and wall references
                for (const auto& visit : path) {
                    const Wall& w = walls[visit.wallIdx];
                    ImVec2 v = visit.reversed ? w.end : w.start;
                    room.vertices.push_back(v);
                    room.wallIds.push_back(w.id);
                    
                    // Set up edge to reference the wall
                    RoomEdge edge;
                    edge.type = RoomEdgeType::FromWall;
                    edge.wallId = w.id;
                    edge.reversed = visit.reversed;
                    room.edges.push_back(edge);
                }
                
                // Verify the room contains the click point
                if (room.containsPoint(clickPoint)) {
                    rooms.push_back(room);
                    return room.id;
                }
            }
        }
        
        return -1;
    }
    
    // Get sampled boundary for a room, using actual wall geometry
    std::vector<ImVec2> getRoomBoundaryWithWalls(const Room& room, int segmentsPerEdge = 20) const {
        std::vector<ImVec2> points;
        if (room.vertices.size() < 3) return points;
        
        size_t n = room.vertices.size();
        for (size_t i = 0; i < n; i++) {
            std::vector<ImVec2> edgePts;
            
            // Check if this edge references a wall
            if (i < room.edges.size() && room.edges[i].type == RoomEdgeType::FromWall) {
                // Find the wall and get its sampled points
                const Wall* wall = nullptr;
                for (const auto& w : walls) {
                    if (w.id == room.edges[i].wallId) {
                        wall = &w;
                        break;
                    }
                }
                
                if (wall) {
                    edgePts = wall->getSampledPoints();
                    if (room.edges[i].reversed) {
                        std::reverse(edgePts.begin(), edgePts.end());
                    }
                }
            } else if (i < room.edges.size() && room.edges[i].type != RoomEdgeType::Straight) {
                // Use the edge's own curve geometry
                const ImVec2& start = room.vertices[i];
                const ImVec2& end = room.vertices[(i + 1) % n];
                edgePts = room.edges[i].getSampledPoints(start, end, segmentsPerEdge);
            }
            
            // Fallback to straight line
            if (edgePts.empty()) {
                edgePts.push_back(room.vertices[i]);
                edgePts.push_back(room.vertices[(i + 1) % n]);
            }
            
            // Add points (except last to avoid duplicates)
            for (size_t j = 0; j < edgePts.size() - 1; j++) {
                points.push_back(edgePts[j]);
            }
        }
        
        // Close the loop
        if (!points.empty()) {
            points.push_back(points[0]);
        }
        
        return points;
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

// ============================================================
// JSON Serialization for Floor Plan structures
// ============================================================
#include <nlohmann/json.hpp>

namespace FloorPlanJSON {
    using json = nlohmann::json;
    
    // ImVec2 serialization
    inline json toJson(const ImVec2& v) {
        return json{{"x", v.x}, {"y", v.y}};
    }
    
    inline ImVec2 vec2FromJson(const json& j) {
        return ImVec2(j.value("x", 0.0f), j.value("y", 0.0f));
    }
    
    // Wall serialization
    inline json toJson(const Wall& w) {
        json j;
        j["id"] = w.id;
        j["start"] = toJson(w.start);
        j["end"] = toJson(w.end);
        j["thickness"] = w.thickness;
        j["color"] = w.color;
        j["type"] = static_cast<int>(w.type);
        j["curveSegments"] = w.curveSegments;
        j["isClosed"] = w.isClosed;
        json cps = json::array();
        for (const auto& cp : w.controlPoints) {
            cps.push_back(toJson(cp));
        }
        j["controlPoints"] = cps;
        return j;
    }
    
    inline Wall wallFromJson(const json& j) {
        Wall w;
        w.id = j.value("id", 0);
        w.start = vec2FromJson(j["start"]);
        w.end = vec2FromJson(j["end"]);
        w.thickness = j.value("thickness", 0.15f);
        w.color = j.value("color", IM_COL32(80, 80, 80, 255));
        w.type = static_cast<WallType>(j.value("type", 0));
        w.curveSegments = j.value("curveSegments", 20);
        w.isClosed = j.value("isClosed", false);
        if (j.contains("controlPoints")) {
            for (const auto& cp : j["controlPoints"]) {
                w.controlPoints.push_back(vec2FromJson(cp));
            }
        }
        return w;
    }
    
    // RoomEdge serialization
    inline json toJson(const RoomEdge& e) {
        json j;
        j["type"] = static_cast<int>(e.type);
        j["wallId"] = e.wallId;
        j["reversed"] = e.reversed;
        json cps = json::array();
        for (const auto& cp : e.controlPoints) {
            cps.push_back(toJson(cp));
        }
        j["controlPoints"] = cps;
        return j;
    }
    
    inline RoomEdge roomEdgeFromJson(const json& j) {
        RoomEdge e;
        e.type = static_cast<RoomEdgeType>(j.value("type", 0));
        e.wallId = j.value("wallId", -1);
        e.reversed = j.value("reversed", false);
        if (j.contains("controlPoints")) {
            for (const auto& cp : j["controlPoints"]) {
                e.controlPoints.push_back(vec2FromJson(cp));
            }
        }
        return e;
    }
    
    // Room serialization
    inline json toJson(const Room& r) {
        json j;
        j["id"] = r.id;
        j["name"] = r.name;
        j["fillColor"] = r.fillColor;
        j["outlineColor"] = r.outlineColor;
        json verts = json::array();
        for (const auto& v : r.vertices) {
            verts.push_back(toJson(v));
        }
        j["vertices"] = verts;
        
        // Serialize edges
        json edgesArr = json::array();
        for (const auto& e : r.edges) {
            edgesArr.push_back(toJson(e));
        }
        j["edges"] = edgesArr;
        
        // Serialize wall IDs
        j["wallIds"] = r.wallIds;
        
        return j;
    }
    
    inline Room roomFromJson(const json& j) {
        Room r;
        r.id = j.value("id", 0);
        r.name = j.value("name", std::string("Room"));
        r.fillColor = j.value("fillColor", IM_COL32(200, 200, 220, 100));
        r.outlineColor = j.value("outlineColor", IM_COL32(100, 100, 150, 255));
        if (j.contains("vertices")) {
            for (const auto& v : j["vertices"]) {
                r.vertices.push_back(vec2FromJson(v));
            }
        }
        if (j.contains("edges")) {
            for (const auto& e : j["edges"]) {
                r.edges.push_back(roomEdgeFromJson(e));
            }
        }
        if (j.contains("wallIds")) {
            r.wallIds = j["wallIds"].get<std::vector<int>>();
        }
        return r;
    }
    
    // Door serialization
    inline json toJson(const Door& d) {
        json j;
        j["id"] = d.id;
        j["wallId"] = d.wallId;
        j["positionOnWall"] = d.positionOnWall;
        j["width"] = d.width;
        j["rotation"] = d.rotation;
        j["swingLeft"] = d.swingLeft;
        j["color"] = d.color;
        return j;
    }
    
    inline Door doorFromJson(const json& j) {
        Door d;
        d.id = j.value("id", 0);
        d.wallId = j.value("wallId", -1);
        d.positionOnWall = j.value("positionOnWall", 0.5f);
        d.width = j.value("width", 0.9f);
        d.rotation = j.value("rotation", 0.0f);
        d.swingLeft = j.value("swingLeft", true);
        d.color = j.value("color", IM_COL32(139, 90, 43, 255));
        return d;
    }
    
    // Window serialization
    inline json toJson(const Window& w) {
        json j;
        j["id"] = w.id;
        j["wallId"] = w.wallId;
        j["positionOnWall"] = w.positionOnWall;
        j["width"] = w.width;
        j["height"] = w.height;
        j["sillHeight"] = w.sillHeight;
        j["color"] = w.color;
        return j;
    }
    
    inline Window windowFromJson(const json& j) {
        Window w;
        w.id = j.value("id", 0);
        w.wallId = j.value("wallId", -1);
        w.positionOnWall = j.value("positionOnWall", 0.5f);
        w.width = j.value("width", 1.2f);
        w.height = j.value("height", 1.0f);
        w.sillHeight = j.value("sillHeight", 0.9f);
        w.color = j.value("color", IM_COL32(135, 206, 235, 200));
        return w;
    }
    
    // Furniture serialization
    inline json toJson(const Furniture& f) {
        json j;
        j["id"] = f.id;
        j["roomId"] = f.roomId;
        j["position"] = toJson(f.position);
        j["size"] = toJson(f.size);
        j["rotation"] = f.rotation;
        j["type"] = f.type;
        j["name"] = f.name;
        j["texture"] = f.texture;
        j["color"] = f.color;
        return j;
    }
    
    inline Furniture furnitureFromJson(const json& j) {
        Furniture f;
        f.id = j.value("id", 0);
        f.roomId = j.value("roomId", -1);
        f.position = vec2FromJson(j["position"]);
        f.size = j.contains("size") ? vec2FromJson(j["size"]) : ImVec2(1, 1);
        f.rotation = j.value("rotation", 0.0f);
        f.type = j.value("type", std::string("generic"));
        f.name = j.value("name", std::string("Furniture"));
        f.texture = j.value("texture", std::string(""));
        f.color = j.value("color", IM_COL32(160, 82, 45, 255));
        return f;
    }
    
    // Staircase serialization
    inline json toJson(const Staircase& s) {
        json j;
        j["id"] = s.id;
        j["position"] = toJson(s.position);
        j["size"] = toJson(s.size);
        j["rotation"] = s.rotation;
        j["connectsToFloor"] = s.connectsToFloor;
        j["numSteps"] = s.numSteps;
        j["color"] = s.color;
        j["wallId"] = s.wallId;
        j["startOnWall"] = s.startOnWall;
        j["endOnWall"] = s.endOnWall;
        return j;
    }
    
    inline Staircase staircaseFromJson(const json& j) {
        Staircase s;
        s.id = j.value("id", 0);
        s.position = vec2FromJson(j["position"]);
        s.size = j.contains("size") ? vec2FromJson(j["size"]) : ImVec2(1, 3);
        s.rotation = j.value("rotation", 0.0f);
        s.connectsToFloor = j.value("connectsToFloor", -1);
        s.numSteps = j.value("numSteps", 12);
        s.color = j.value("color", IM_COL32(105, 105, 105, 255));
        s.wallId = j.value("wallId", -1);
        s.startOnWall = j.value("startOnWall", 0.0f);
        s.endOnWall = j.value("endOnWall", 1.0f);
        return s;
    }
    
    // FloorPlan serialization
    inline json toJson(const FloorPlan& fp) {
        json j;
        j["gridSize"] = fp.gridSize;
        j["snapToGrid"] = fp.snapToGrid;
        j["units"] = fp.units;
        j["unitsPerMeter"] = fp.unitsPerMeter;
        j["nextWallId"] = fp.nextWallId;
        j["nextRoomId"] = fp.nextRoomId;
        j["nextDoorId"] = fp.nextDoorId;
        j["nextWindowId"] = fp.nextWindowId;
        j["nextFurnitureId"] = fp.nextFurnitureId;
        j["nextStaircaseId"] = fp.nextStaircaseId;
        
        json walls = json::array();
        for (const auto& w : fp.walls) walls.push_back(toJson(w));
        j["walls"] = walls;
        
        json rooms = json::array();
        for (const auto& r : fp.rooms) rooms.push_back(toJson(r));
        j["rooms"] = rooms;
        
        json doors = json::array();
        for (const auto& d : fp.doors) doors.push_back(toJson(d));
        j["doors"] = doors;
        
        json windows = json::array();
        for (const auto& w : fp.windows) windows.push_back(toJson(w));
        j["windows"] = windows;
        
        json furniture = json::array();
        for (const auto& f : fp.furniture) furniture.push_back(toJson(f));
        j["furniture"] = furniture;
        
        json staircases = json::array();
        for (const auto& s : fp.staircases) staircases.push_back(toJson(s));
        j["staircases"] = staircases;
        
        return j;
    }
    
    inline FloorPlan floorPlanFromJson(const json& j) {
        FloorPlan fp;
        fp.gridSize = j.value("gridSize", 0.5f);
        fp.snapToGrid = j.value("snapToGrid", true);
        fp.units = j.value("units", std::string("m"));
        fp.unitsPerMeter = j.value("unitsPerMeter", 1.0f);
        fp.nextWallId = j.value("nextWallId", 1);
        fp.nextRoomId = j.value("nextRoomId", 1);
        fp.nextDoorId = j.value("nextDoorId", 1);
        fp.nextWindowId = j.value("nextWindowId", 1);
        fp.nextFurnitureId = j.value("nextFurnitureId", 1);
        fp.nextStaircaseId = j.value("nextStaircaseId", 1);
        
        if (j.contains("walls")) {
            for (const auto& w : j["walls"]) fp.walls.push_back(wallFromJson(w));
        }
        if (j.contains("rooms")) {
            for (const auto& r : j["rooms"]) fp.rooms.push_back(roomFromJson(r));
        }
        if (j.contains("doors")) {
            for (const auto& d : j["doors"]) fp.doors.push_back(doorFromJson(d));
        }
        if (j.contains("windows")) {
            for (const auto& w : j["windows"]) fp.windows.push_back(windowFromJson(w));
        }
        if (j.contains("furniture")) {
            for (const auto& f : j["furniture"]) fp.furniture.push_back(furnitureFromJson(f));
        }
        if (j.contains("staircases")) {
            for (const auto& s : j["staircases"]) fp.staircases.push_back(staircaseFromJson(s));
        }
        
        return fp;
    }
    
    // Serialize FloorPlan to JSON string
    inline std::string serialize(const FloorPlan& fp) {
        return toJson(fp).dump();
    }
    
    // Deserialize FloorPlan from JSON string
    inline FloorPlan deserialize(const std::string& jsonStr) {
        return floorPlanFromJson(json::parse(jsonStr));
    }
}