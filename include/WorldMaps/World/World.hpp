#pragma once
#include <unordered_map>
#include <memory>
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/Map/ElevationLayer.hpp>
#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/World/Projection.hpp>
//#include <WorldMaps/World/Chunk.hpp>

class World{
public:
    World(){
        //initialize with default layers
        addLayer("elevation", std::make_unique<ElevationLayer>());
        addLayer("humidity", std::make_unique<HumidityLayer>());
        addLayer("temperature", std::make_unique<TemperatureLayer>());
    }
    ~World() = default;
    SampleData sample(float longitude, float latitude, const std::string& layerName="") const{
        auto it = layers.find(layerName);
        if(it != layers.end()){
            return it->second->sample(longitude, latitude);
        }
        //if map layer not found and we have at least one layer, use the first one
        if(!layers.empty()){
            return layers.begin()->second->sample(longitude, latitude);
        }
        else{
            return SampleData{}; //empty sample data
        }
    }
    std::array<uint8_t, 4> getColor(float longitude, float latitude, const std::string& layerName="") const{
        auto it = layers.find(layerName);
        if(it != layers.end()){
            return it->second->getColor(longitude, latitude);
        }
        //if map layer not found and we have at least one layer, use the first one
        if(!layers.empty()){
            return layers.begin()->second->getColor(longitude, latitude);
        }
        else{
            return {0,0,0,255}; //default to black
        }
    }
    void addLayer(const std::string& name, std::unique_ptr<MapLayer> layer){
        layers[name] = std::move(layer);
    }
private:
    std::unordered_map<std::string, std::unique_ptr<MapLayer>> layers;
//    std::unordered_map<std::pair<int,int>, Chunk> chunks;
};