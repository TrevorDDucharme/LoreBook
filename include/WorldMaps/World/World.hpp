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
//#include <WorldMaps/World/Chunk.hpp>

class World{
public:
    World(){
        //initialize with default layers
        addLayer("elevation", std::make_unique<ElevationLayer>());
        addLayer("humidity", std::make_unique<HumidityLayer>());
        addLayer("temperature", std::make_unique<TemperatureLayer>());
        addLayer("color", std::make_unique<ColorLayer>());
        addLayer("biome", std::make_unique<BiomeLayer>());
    }
    ~World() = default;
    SampleData sample(const std::string& layerName="") const{
        auto it = layers.find(layerName);
        if(it != layers.end()){
            return it->second->sample(*this);
        }
        //if map layer not found and we have at least one layer, use the first one
        if(!layers.empty()){
            return layers.begin()->second->sample(*this);
        }
        else{
            return SampleData{}; //empty sample data
        }
    }
    cl_mem getColor(const std::string& layerName="") const{
        auto it = layers.find(layerName);
        if(it != layers.end()){
            return it->second->getColor(*this);
        }
        //if map layer not found and we have at least one layer, use the first one
        if(!layers.empty()){
            return layers.begin()->second->getColor(*this);
        }
        else{
            return nullptr; //empty color
        }
    }
    void addLayer(const std::string& name, std::unique_ptr<MapLayer> layer){
        layers[name] = std::move(layer);
    }

    // Access raw layer pointer for runtime tweaks (returns nullptr if not found)
    MapLayer* getLayer(const std::string& name){
        auto it = layers.find(name);
        return (it != layers.end()) ? it->second.get() : nullptr;
    }
    const MapLayer* getLayer(const std::string& name) const{
        auto it = layers.find(name);
        return (it != layers.end()) ? it->second.get() : nullptr;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<MapLayer>> layers;
//    std::unordered_map<std::pair<int,int>, Chunk> chunks;
};