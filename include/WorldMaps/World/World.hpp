#pragma once
#include <unordered_map>
#include <memory>
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/Map/ElevationLayer.hpp>
#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/Map/WaterTableLayer.hpp>
#include <WorldMaps/Map/LandTypeLayer.hpp>
#include <WorldMaps/Map/RiverLayer.hpp>
#include <WorldMaps/Map/LatitudeLayer.hpp>
#include <memory>
#include <stack>
#include <stringUtils.hpp>
// #include <WorldMaps/World/Chunk.hpp>

class World
{
public:
    World()=default;
    World(std::string config)
    {
        parseConfig(config);
    }
    ~World() = default;
    cl_mem sample(const std::string &layerName = "") const
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
            return cl_mem{}; // empty sample data
        }
    }
    cl_mem getColor(const std::string &layerName = "") const
    {
        ZoneScopedN("World::getColor");
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

    int getWorldLatitudeResolution() const { return worldLatitudeResolution; }
    int getWorldLongitudeResolution() const { return worldLongitudeResolution; }

private:

    int worldLatitudeResolution=4096;
    int worldLongitudeResolution=4096;

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
            else if (layerName == "watertable")
            {
                auto layer = std::make_unique<WaterTableLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "landtype")
            {
                auto layer = std::make_unique<LandTypeLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "river")
            {
                auto layer = std::make_unique<RiverLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            // Add more layers as needed
        }     
        


    }

    std::unordered_map<std::string, std::unique_ptr<MapLayer>> layers;
    //    std::unordered_map<std::pair<int,int>, Chunk> chunks;
};