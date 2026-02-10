#pragma once
#include <imgui.h>
#include <WorldMaps/World/World.hpp>
#include <WorldMaps/World/Projections/MercatorProjection.hpp>
#include <WorldMaps/World/Projections/SphereProjection.hpp>

class Vault;

void mercatorMap(const char *label, ImVec2 texSize, World &world);

void globeMap(const char *label, ImVec2 texSize, World &world);

void worldMap(bool& m_isOpen, Vault* vault = nullptr);