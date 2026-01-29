#pragma once
#include <cmath>
#include <string>
#include <GL/glew.h>
#include <future>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <array>
#include <plog/Log.h>

class World;

class Projection
{
public:
    Projection()= default;
    virtual ~Projection()= default;
    virtual GLuint project(World &world, int width, int height, std::string layerName = "") = 0;
};