#ifndef NETGEN_JULIA_CSG_HPP
#define NETGEN_JULIA_CSG_HPP

// Julia (CxxWrap/JlCxx) binding for a minimal CSG mesh-generation path: a
// netgen::CSGeometry handle, an axis-aligned box primitive, and GenerateMesh
// driven by MeshingParameters. The resulting Mesh is consumed with the Sprint 3
// extraction functions.

#include <jlcxx/jlcxx.hpp>

namespace netgen_julia
{
  void ExportJuliaCSG(jlcxx::Module& mod);
}

#endif // NETGEN_JULIA_CSG_HPP
