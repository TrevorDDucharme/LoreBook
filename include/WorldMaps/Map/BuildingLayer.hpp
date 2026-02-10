#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/Buildings/FloorPlan.hpp>
#include <vector>
#include <random>

class Vault;

// A floorplan placed at a specific location on the world map.
struct PlacedBuilding {
    float mapX = 0.0f;      // adjusted origin in map cells (latitude axis)
    float mapY = 0.0f;      // adjusted origin in map cells (longitude axis)
    float scatterX = 0.0f;  // original scatter centre (for min-distance)
    float scatterY = 0.0f;
    FloorPlan plan;
    // Bounding box in map coordinates (recomputed after placement)
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};

/// Map layer that loads FloorPlan templates from the vault,
/// scatters them randomly across the map, and renders full-detail
/// footprints (rooms, walls, doors, windows, furniture, staircases)
/// via an OpenCL kernel.
class BuildingLayer : public MapLayer
{
public:
    BuildingLayer() = default;
    ~BuildingLayer() override;

    cl_mem sample() override;
    cl_mem getColor() override;

    /// Parse configuration parameters.
    /// Format: "minDistance:100,maxBuildings:200,seed:42,cellsPerMeter:3.0"
    void parseParameters(const std::string& params) override;

    /// Set floorplan templates directly (alternative to vault loading).
    void setTemplates(const std::vector<FloorPlan>& templates);

private:
    void loadTemplatesAndScatter();
    void rasterizeToGPU();
    void computeBuildingBounds(PlacedBuilding& b);

    // Placed buildings with their floorplans and map positions
    std::vector<PlacedBuilding> placedBuildings;

    // Available templates loaded from vault
    std::vector<FloorPlan> templates_;
    bool templatesLoaded_ = false;

    // Output buffer
    cl_mem colorBuffer_ = nullptr;
    bool dirty_ = true;

    // Scatter parameters
    float minDistance_    = 250.0f;  // min distance (cells) between buildings
    int   maxBuildings_  = 200;     // max buildings to place
    unsigned int seed_   = 42;      // RNG seed
    float cellsPerMeter_ = 10.0f;  // scale: map cells per floorplan meter
};
