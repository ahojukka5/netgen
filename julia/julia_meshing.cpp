#include "julia_meshing.hpp"

// Pulls in netgen::Point3d / netgen::Vec3d (libsrc/gprim/geom3d.hpp) and
// netgen::MeshingParameters (libsrc/meshing/meshtype.hpp). This is the same
// umbrella header the Python binding (python_mesh.cpp) includes.
#include <meshing.hpp>

namespace netgen_julia
{
  using netgen::Point3d;
  using netgen::Vec3d;
  using netgen::MeshingParameters;

  void ExportJuliaMeshing(jlcxx::Module& mod)
  {
    // --- 3D point: netgen::Point3d -----------------------------------------
    // Accessors are exposed as free functions x/y/z (Julia: x(p), y(p), z(p)).
    // Netgen's X()/Y()/Z() are 0-based component accessors; we expose them by
    // name, so there is no index convention to worry about here.
    mod.add_type<Point3d>("Point3d")
        .constructor<double, double, double>()
        .method("x", [](const Point3d& p) { return p.X(); })
        .method("y", [](const Point3d& p) { return p.Y(); })
        .method("z", [](const Point3d& p) { return p.Z(); });

    // --- 3D vector: netgen::Vec3d ------------------------------------------
    mod.add_type<Vec3d>("Vec3d")
        .constructor<double, double, double>()
        .method("x", [](const Vec3d& v) { return v.X(); })
        .method("y", [](const Vec3d& v) { return v.Y(); })
        .method("z", [](const Vec3d& v) { return v.Z(); })
        .method("length", [](const Vec3d& v) { return v.Length(); });

    // --- MeshingParameters -------------------------------------------------
    // The Python wrapper offers rich keyword parsing; here we expose only a few
    // public fields through explicit getters/setters. Start with maxh.
    mod.add_type<MeshingParameters>("MeshingParameters")
        .constructor<>()
        .method("maxh", [](const MeshingParameters& mp) { return mp.maxh; })
        .method("set_maxh!", [](MeshingParameters& mp, double h) { mp.maxh = h; })
        .method("minh", [](const MeshingParameters& mp) { return mp.minh; })
        .method("set_minh!", [](MeshingParameters& mp, double h) { mp.minh = h; })
        .method("grading", [](const MeshingParameters& mp) { return mp.grading; })
        .method("set_grading!", [](MeshingParameters& mp, double g) { mp.grading = g; })
        .method("secondorder", [](const MeshingParameters& mp) { return mp.secondorder; })
        .method("set_secondorder!", [](MeshingParameters& mp, bool b) { mp.secondorder = b; });
  }
}
