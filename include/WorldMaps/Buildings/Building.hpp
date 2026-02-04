#pragma once
#include <WorldMaps/Buildings/Floor.hpp>
#include <WorldMaps/Buildings/FloorPlan.hpp>
#include <vector>
#include <string>
#include <algorithm>

// ============================================================
// Building - Contains multiple floors
// ============================================================
class Building
{
public:
    Building() = default;
    Building(const std::string& name) : m_name(name) {}
    
    // Basic properties
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    
    int getId() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    // Floor management
    size_t getFloorCount() const { return m_floors.size(); }
    
    Floor& getFloor(size_t index) { return m_floors[index]; }
    const Floor& getFloor(size_t index) const { return m_floors[index]; }
    
    Floor* getCurrentFloor() {
        if (m_currentFloor >= 0 && m_currentFloor < (int)m_floors.size())
            return &m_floors[m_currentFloor];
        return nullptr;
    }
    
    int getCurrentFloorIndex() const { return m_currentFloor; }
    void setCurrentFloor(int index) {
        if (index >= 0 && index < (int)m_floors.size())
            m_currentFloor = index;
    }
    
    // Add a new floor
    Floor& addFloor(int level = -999) {
        Floor f;
        f.id = m_nextFloorId++;
        
        // Auto-assign level if not specified
        if (level == -999) {
            if (m_floors.empty()) {
                f.level = 0;
            } else {
                // Find highest level and add above it
                int maxLevel = m_floors[0].level;
                for (const auto& fl : m_floors) {
                    if (fl.level > maxLevel) maxLevel = fl.level;
                }
                f.level = maxLevel + 1;
            }
        } else {
            f.level = level;
        }
        
        // Set elevation based on level
        f.elevation = f.level * 3.0f; // Assume 3m per floor
        
        // Set default name
        f.name = f.displayName();
        
        m_floors.push_back(f);
        
        // Sort floors by level
        std::sort(m_floors.begin(), m_floors.end(), 
            [](const Floor& a, const Floor& b) { return a.level < b.level; });
        
        // Set current floor to the new one
        for (size_t i = 0; i < m_floors.size(); i++) {
            if (m_floors[i].id == f.id) {
                m_currentFloor = static_cast<int>(i);
                break;
            }
        }
        
        return m_floors[m_currentFloor];
    }
    
    // Add a basement (negative level)
    Floor& addBasement() {
        // Find lowest level and add below it
        int minLevel = 0;
        for (const auto& fl : m_floors) {
            if (fl.level < minLevel) minLevel = fl.level;
        }
        return addFloor(minLevel - 1);
    }
    
    // Add floor above current highest
    Floor& addFloorAbove() {
        int maxLevel = 0;
        for (const auto& fl : m_floors) {
            if (fl.level > maxLevel) maxLevel = fl.level;
        }
        return addFloor(maxLevel + 1);
    }
    
    // Get basement count
    int getBasementCount() const {
        int count = 0;
        for (const auto& f : m_floors) {
            if (f.level < 0) count++;
        }
        return count;
    }
    
    // Get above-ground floor count
    int getAboveGroundCount() const {
        int count = 0;
        for (const auto& f : m_floors) {
            if (f.level >= 0) count++;
        }
        return count;
    }
    
    // Remove floor by index
    bool removeFloor(size_t index) {
        if (index >= m_floors.size()) return false;
        if (m_floors.size() <= 1) return false; // Keep at least one floor
        
        m_floors.erase(m_floors.begin() + index);
        if (m_currentFloor >= (int)m_floors.size()) {
            m_currentFloor = (int)m_floors.size() - 1;
        }
        return true;
    }
    
    // Find floor by level
    Floor* findFloorByLevel(int level) {
        for (auto& f : m_floors) {
            if (f.level == level) return &f;
        }
        return nullptr;
    }
    
    // Get all floors (for iteration)
    std::vector<Floor>& getFloors() { return m_floors; }
    const std::vector<Floor>& getFloors() const { return m_floors; }
    
    // Clear building
    void clear() {
        m_floors.clear();
        m_currentFloor = -1;
        m_nextFloorId = 1;
        addFloor(0); // Add ground floor
    }
    
    // Initialize with default ground floor
    void initialize() {
        if (m_floors.empty()) {
            addFloor(0);
        }
    }

private:
    int m_id = 0;
    std::string m_name = "New Building";
    std::vector<Floor> m_floors;
    int m_currentFloor = -1;
    int m_nextFloorId = 1;
};