#ifndef NETGEN_JULIA_TOPOLOGY_HPP
#define NETGEN_JULIA_TOPOLOGY_HPP

// Julia (CxxWrap/JlCxx) bindings for generic Netgen mesh topology / incidence:
// global edges and faces, volume-element -> edge/face maps, edge orientation
// signs, and the surface-element -> global-face map. Read-only, copied into
// Julia arrays; downstream codes build their own transfer/layout structures.

#include <jlcxx/jlcxx.hpp>

namespace netgen_julia
{
  void ExportJuliaTopology(jlcxx::Module& mod);
}

#endif // NETGEN_JULIA_TOPOLOGY_HPP
