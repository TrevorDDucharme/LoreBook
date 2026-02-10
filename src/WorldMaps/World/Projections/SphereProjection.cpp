#include <WorldMaps/World/Projections/SphereProjection.hpp>

cl_program SphericalProjection::spherePerspectiveProgram = nullptr;
cl_kernel SphericalProjection::spherePerspectiveKernel = nullptr;
cl_kernel SphericalProjection::spherePerspectiveRegionKernel = nullptr;