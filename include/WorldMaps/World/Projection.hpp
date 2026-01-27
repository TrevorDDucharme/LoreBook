#pragma once
#include <cmath>
#include <string>
#include <GL/glew.h>

class World;

class Projection {
public:
    Projection();
    virtual ~Projection();
    // Creates an OpenGL texture for the projected view and returns its GLuint.
    // NOTE: This must be called from a valid OpenGL context (GL thread).
    virtual GLuint project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height,std::string layerName="") const = 0;
};

class MercatorProjection : public Projection {
public:
    MercatorProjection();
    ~MercatorProjection() override;
    GLuint project(const World& world, float longitude, float latitude, float zoomLevel, int width, int height,std::string layerName="") const override;
};