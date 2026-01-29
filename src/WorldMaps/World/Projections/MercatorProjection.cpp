#include <WorldMaps/World/Projections/MercatorProjection.hpp>

cl_program MercatorProjection::mercatorProgram = nullptr;
cl_kernel MercatorProjection::mercatorKernel = nullptr;