#ifndef NETGEN_JULIA_MESH_HPP
#define NETGEN_JULIA_MESH_HPP

// Julia (CxxWrap/JlCxx) binding for a minimal netgen::Mesh handle: lifetime via
// std::shared_ptr (the same holder Python uses), construction/loading, saving,
// read-only count queries, and read-only bulk extraction of coordinates and
// element connectivity/types.

#include <jlcxx/jlcxx.hpp>

namespace netgen_julia
{
  void ExportJuliaMesh(jlcxx::Module& mod);
}

#endif // NETGEN_JULIA_MESH_HPP
