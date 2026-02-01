#pragma once
#include <WorldMaps/World/Projections/Projection.hpp>
#include <WorldMaps/World/World.hpp>

class MercatorProjection : public Projection
{
public:
    MercatorProjection() = default;
    ~MercatorProjection() override;

    // Camera controls (center in radians, zoom > 0: larger = closer)
    void setViewCenterRadians(float lonRad, float latRad);

    void setZoomLevel(float z);
    //caller must cleanup texture when done
    void project(World &world, int width, int height, GLuint& texture, std::string layerName="") override;

private:
    GLuint texture = 0;
    cl_mem mercatorBuffer = nullptr;

    // Camera state (radians/mercator y/zoom): set via setters above
    float centerLon = 0.0f;
    float centerLat = 0.0f;
    float centerMercY = 0.0f;
    float zoomLevel = 1.0f;

    static cl_program mercatorProgram;
    static cl_kernel mercatorKernel;

    static void mercatorProject(
        cl_mem& output,
        cl_mem field3d,
        int fieldW,
        int fieldH,
        int fieldD,
        int outW,
        int outH,
        float radius,
        float centerLon,
        float centerMercY,
        float zoom);
};