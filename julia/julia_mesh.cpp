#include "julia_mesh.hpp"

// netgen::Mesh and its count/IO members (libsrc/meshing/meshclass.hpp), pulled
// in via the same umbrella header the Python binding uses.
#include <meshing.hpp>

namespace netgen_julia
{
  using netgen::Mesh;
  using MeshPtr = std::shared_ptr<Mesh>;

  void ExportJuliaMesh(jlcxx::Module& mod)
  {
    // Ownership model: std::shared_ptr<Mesh>, matching Python's
    // py::class_<Mesh, shared_ptr<Mesh>> holder. The Mesh type is registered so
    // that shared_ptr<Mesh> is handled as a smart pointer; every function below
    // takes/returns the shared_ptr so Julia never sees a raw owning pointer.
    mod.add_type<Mesh>("Mesh");

    // --- factories ---------------------------------------------------------
    // Empty default mesh (0 points / elements). Unlike nglib's Ng_NewMesh we do
    // not add a default FaceDescriptor: it is only needed before adding surface
    // elements, which this sprint does not do.
    mod.method("new_mesh", []() -> MeshPtr { return std::make_shared<Mesh>(); });

    mod.method("load_mesh", [](const std::string& filename) -> MeshPtr {
      auto m = std::make_shared<Mesh>();
      m->Load(filename);  // const filesystem::path& overload
      return m;
    });

    // --- read-only count queries ------------------------------------------
    mod.method("num_points", [](const MeshPtr& m) {
      return static_cast<int64_t>(m->GetNP());
    });
    mod.method("num_volume_elements", [](const MeshPtr& m) {
      return static_cast<int64_t>(m->GetNE());
    });
    mod.method("num_surface_elements", [](const MeshPtr& m) {
      return static_cast<int64_t>(m->GetNSE());
    });
    mod.method("num_segments", [](const MeshPtr& m) {
      return static_cast<int64_t>(m->GetNSeg());
    });

    // --- save --------------------------------------------------------------
    mod.method("save_mesh", [](const MeshPtr& m, const std::string& filename) {
      m->Save(filename);  // const filesystem::path& overload
    });
  }
}
