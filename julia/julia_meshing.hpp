#ifndef NETGEN_JULIA_MESHING_HPP
#define NETGEN_JULIA_MESHING_HPP

// Julia (CxxWrap/JlCxx) bindings for simple Netgen meshing value/configuration
// types. Mirrors the role of libsrc/meshing/python_mesh.cpp on the Python side,
// but is intentionally minimal for now (Point3d, Vec3d, MeshingParameters).

#include <jlcxx/jlcxx.hpp>

namespace netgen_julia
{
  // Register the meshing value types and parameters on the given CxxWrap module.
  void ExportJuliaMeshing(jlcxx::Module& mod);
}

#endif // NETGEN_JULIA_MESHING_HPP
