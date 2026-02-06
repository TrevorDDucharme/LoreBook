#pragma once
#include <string>
#include <Editors/Java/ProgramStructure/Type.hpp>

struct Variable
{
    Type type;
    std::string name;
    std::string value;
    bool isConstant = false;
    bool isStatic = false;
    bool isFinal = false;
    bool isTransient = false;
    bool isVolatile = false;
    bool isPublic = false;
    bool isPrivate = false;
    bool isProtected = false;
};