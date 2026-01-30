#pragma once
#include <unordered_map>
#include <memory>
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/Map/ElevationLayer.hpp>
#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/Map/WaterLayer.hpp>
#include <WorldMaps/Map/BiomeLayer.hpp>
#include <WorldMaps/Map/RiverLayer.hpp>
#include <memory>
#include <stack>
#include <stringUtils.hpp>
// #include <WorldMaps/World/Chunk.hpp>

class World
{
public:
    World()
    {
        // initialize with default layers
        //addLayer("elevation", std::make_unique<ElevationLayer>());
        // addLayer("humidity", std::make_unique<HumidityLayer>());
        // addLayer("temperature", std::make_unique<TemperatureLayer>());
        // addLayer("color", std::make_unique<ColorLayer>());
        // addLayer("biome", std::make_unique<BiomeLayer>());
    }
    World(std::string config)
    {
        parseConfig(config);
    }
    ~World() = default;
    SampleData sample(const std::string &layerName = "") const
    {
        auto it = layers.find(layerName);
        if (it != layers.end())
        {
            return it->second->sample();
        }
        // if map layer not found and we have at least one layer, use the first one
        if (!layers.empty())
        {
            return layers.begin()->second->sample();
        }
        else
        {
            return SampleData{}; // empty sample data
        }
    }
    cl_mem getColor(const std::string &layerName = "") const
    {
        auto it = layers.find(layerName);
        if (it != layers.end())
        {
            return it->second->getColor();
        }
        // if map layer not found and we have at least one layer, use the first one
        if (!layers.empty())
        {
            return layers.begin()->second->getColor();
        }
        else
        {
            return nullptr; // empty color
        }
    }

    void addLayer(const std::string &name, std::unique_ptr<MapLayer> layer)
    {
        layer->setParentWorld(this);
        layers[name] = std::move(layer);
    }

    // Access raw layer pointer for runtime tweaks (returns nullptr if not found)
    MapLayer *getLayer(const std::string &name)
    {
        auto it = layers.find(name);
        return (it != layers.end()) ? it->second.get() : nullptr;
    }
    const MapLayer *getLayer(const std::string &name) const
    {
        auto it = layers.find(name);
        return (it != layers.end()) ? it->second.get() : nullptr;
    }

    std::vector<std::string> getLayerNames()
    {
        std::vector<std::string> names;
        for (const auto &pair : layers)
        {
            names.push_back(pair.first);
        }
        return names;
    }

    int getWorldWidth() const { return worldW; }
    int getWorldHeight() const { return worldH; }
    int getWorldDepth() const { return worldD; }

private:

    int worldW=256;
    int worldH=256;
    int worldD=256;

    //Biome(count:2,colors:[{0,0,255},{0,255,0}]),Water(Level:1.3),Humidity,Temperature
    void parseConfig(const std::string &config)
    {
        std::vector<std::string> layerConfigs = splitBracketAware(config, ",");
        for (const auto &layerConfig : layerConfigs)
        {
            std::string layerName;
            std::string layerParams;
            splitNameConfig(layerConfig, layerName, layerParams);
            //to lowercase layerName for comparison
            std::transform(layerName.begin(), layerName.end(), layerName.begin(), ::tolower);

            if (layerName == "elevation")
            {
                addLayer(layerName, std::make_unique<ElevationLayer>());
            }
            else if (layerName == "humidity")
            {
                addLayer(layerName, std::make_unique<HumidityLayer>());
            }
            else if (layerName == "temperature")
            {
                addLayer(layerName, std::make_unique<TemperatureLayer>());
            }
            else if (layerName == "color")
            {
                addLayer(layerName, std::make_unique<ColorLayer>());
            }
            else if (layerName == "water")
            {
                auto layer = std::make_unique<WaterLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "biome")
            {
                auto layer = std::make_unique<BiomeLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            // Add more layers as needed
        }     
        


    }

    std::unordered_map<std::string, std::unique_ptr<MapLayer>> layers;
    //    std::unordered_map<std::pair<int,int>, Chunk> chunks;
};