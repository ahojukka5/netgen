/*
  Experimental, opt-in Julia entry point for Netgen (built only with USE_JULIA=ON).

  This mirrors the idea of the Python entry point ng/netgenpy.cpp, but uses
  CxxWrap/JlCxx instead of pybind11. It is the single CxxWrap module definition;
  the actual bindings live in the ExportJulia* functions called below. The
  wrapped surface is intentionally small (see julia/README.md): two trivial
  smoke functions, basic meshing value types, a Mesh handle with read-only
  extraction, a minimal CSG mesh-generation path, and (only when USE_OCC=ON)
  minimal OCC file import. It is not a full Netgen Julia API.
*/

#include <string>

#include <jlcxx/jlcxx.hpp>

#include "julia_meshing.hpp"
#include "julia_mesh.hpp"
#include "julia_topology.hpp"
#include "julia_csg.hpp"
#ifdef NGJL_HAS_OCC
#include "julia_occ.hpp"
#endif

namespace netgen_julia
{
  // Trivial smoke function: returns a fixed value Julia can assert on.
  int netgen_julia_smoke()
  {
    return 42;
  }

  // Second trivial function returning a string, to exercise std::string marshalling.
  std::string netgen_julia_hello()
  {
    return "netgen-julia-ok";
  }
}

JLCXX_MODULE define_julia_module(jlcxx::Module& mod)
{
  mod.method("netgen_julia_smoke", &netgen_julia::netgen_julia_smoke);
  mod.method("netgen_julia_hello", &netgen_julia::netgen_julia_hello);

  // First real value-type bindings (Point3d, Vec3d, MeshingParameters).
  netgen_julia::ExportJuliaMeshing(mod);

  // Minimal Mesh handle (shared_ptr lifetime), load/save and count queries.
  netgen_julia::ExportJuliaMesh(mod);

  // Generic topology / incidence: edges, faces, element-to-edge/face maps.
  netgen_julia::ExportJuliaTopology(mod);

  // Minimal CSG geometry + mesh generation (unit cube / axis-aligned box).
  netgen_julia::ExportJuliaCSG(mod);

#ifdef NGJL_HAS_OCC
  // Minimal OCC import (STEP/BREP/IGES -> OCCGeometry -> GenerateMesh).
  netgen_julia::ExportJuliaOCC(mod);
#endif
}
