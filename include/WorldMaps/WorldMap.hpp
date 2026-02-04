#pragma once
#include <imgui.h>
#include <WorldMaps/World/World.hpp>
#include <WorldMaps/World/Projections/MercatorProjection.hpp>
#include <WorldMaps/World/Projections/SphereProjection.hpp>

void mercatorMap(const char *label, ImVec2 texSize, World &world);

void globeMap(const char *label, ImVec2 texSize, World &world);

void worldMap(bool& m_isOpen);