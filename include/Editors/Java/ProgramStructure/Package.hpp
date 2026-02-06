#pragma once
#include <Editors/Java/ProgramStructure/Class.hpp>

struct Package{
    std::string name; // Package name
    std::vector<Class> classes; // Classes in this package
    std::vector<Package> subPackages; // Sub-packages within this package
};