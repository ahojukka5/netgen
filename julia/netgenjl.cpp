/*
  Experimental, opt-in Julia entry point for Netgen (built only with USE_JULIA=ON).

  This mirrors the existing Python entry point ng/netgenpy.cpp, but uses
  CxxWrap/JlCxx instead of pybind11. For this first sprint it is intentionally a
  minimal smoke module: it wraps a single trivial function so that we can prove
  the build, load and call path from Julia works end to end. No Netgen classes
  are wrapped yet.
*/

#include <string>

#include <jlcxx/jlcxx.hpp>

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
}
