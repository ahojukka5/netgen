#ifndef NETGEN_JULIA_MESH_HPP
#define NETGEN_JULIA_MESH_HPP

// Julia (CxxWrap/JlCxx) binding for a minimal netgen::Mesh handle: lifetime via
// std::shared_ptr (the same holder Python uses), construction/loading, saving
// and read-only count queries. No coordinate or connectivity extraction yet.

#include <jlcxx/jlcxx.hpp>

namespace netgen_julia
{
  void ExportJuliaMesh(jlcxx::Module& mod);
}

#endif // NETGEN_JULIA_MESH_HPP
