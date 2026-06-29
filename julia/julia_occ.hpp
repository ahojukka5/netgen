#ifndef NETGEN_JULIA_OCC_HPP
#define NETGEN_JULIA_OCC_HPP

// Julia (CxxWrap/JlCxx) binding for a minimal OpenCASCADE import path: load an
// OCC geometry from a STEP/BREP/IGES file and generate a Mesh from it. Compiled
// only when USE_OCC=ON (see julia/CMakeLists.txt -> NGJL_HAS_OCC). Read-only and
// deliberately tiny: no shape/topology/boolean API.

#include <jlcxx/jlcxx.hpp>

namespace netgen_julia
{
  void ExportJuliaOCC(jlcxx::Module& mod);
}

#endif // NETGEN_JULIA_OCC_HPP
