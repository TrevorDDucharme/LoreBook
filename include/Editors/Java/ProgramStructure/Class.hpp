#pragma once
#include <Editors/Java/ProgramStructure/Variable.hpp>
#include <Editors/Java/ProgramStructure/Method.hpp>

struct Class
{
    std::string name;
    std::string extendsClass; // Parent class name
    std::vector<std::string> implementsInterfaces; // Implemented interfaces
    std::vector<Variable> variables;
    std::vector<Method> methods;
};