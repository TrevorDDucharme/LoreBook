#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

// POD struct matching OpenCL kernel layout
// Must match typedef struct TecVoxel in Tectonics.cl exactly
struct TecVoxel {
    float velX;         // tangent velocity X
    float velY;         // tangent velocity Y
    float pressure;     // accumulated pressure
    int plateId;        // which plate this belongs to
};

class TectonicsLayer : public MapLayer
{
public:
    TectonicsLayer() = default;
    ~TectonicsLayer() override;
    
    cl_mem sample() override;
    cl_mem getColor() override;
    void parseParameters(const std::string &params) override;

private:
    // Resolution settings
    size_t uvResolution = 512;       // resolution per cubemap face
    
    // Simulation control
    int simulationSteps = 500;       // steps to run
    bool simulationComplete = false;
    
    // Physics parameters
    float dt = 0.5f;                 // timestep
    
    // Plate config
    unsigned int seed = 12345;
    int numPlates = 12;
    
    // OpenCL buffers - ping-pong for simulation
    cl_mem voxelBufferA = nullptr;
    cl_mem voxelBufferB = nullptr;
    
    // Output buffers
    cl_mem heightmapBuffer = nullptr;    // 2D lat/lon heightmap (float)
    cl_mem coloredBuffer = nullptr;      // RGBA visualization
    cl_mem surfaceBuffer = nullptr;      // cubemap surface heights
    
    // Private methods
    bool initializeSimulation();
    void runSimulation(int steps);
    void extractHeightmap();
    void releaseBuffers();
    
    // Cubemap to lat/lon projection
    void cubemapToLatLon(cl_mem cubemapSurface, cl_mem& latlonOutput,
                         int latRes, int lonRes);
};