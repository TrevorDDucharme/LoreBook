#pragma once
#include <Editors/Java/ProgramStructure/Type.hpp>
#include <vector>

struct Method
{
    Type returnType;
    std::string name;
    std::vector<std::pair<Type, std::string>> parameters; // Pair of type and parameter name
    bool isStatic = false;
    bool isFinal = false;
    bool isAbstract = false;
    bool isSynchronized = false;
    bool isPublic = false;
    bool isPrivate = false;
    bool isProtected = false;
    bool isNative = false;
    bool isStrictfp = false;
};