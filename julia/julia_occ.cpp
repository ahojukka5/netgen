#include "julia_occ.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

// netgen::OCCGeometry and the LoadOCC_* loaders (libsrc/occ/occgeom.hpp). The
// OCCGeometry class and these loaders are already exported from nglib
// (DLL_HEADER), so this binding needs no new exported symbols. Requires the
// OCCGEOMETRY macro (set globally when USE_OCC=ON).
#include <occgeom.hpp>

namespace netgen_julia
{
  using netgen::OCCGeometry;
  using netgen::Mesh;
  using netgen::MeshingParameters;
  using OCCPtr = std::shared_ptr<OCCGeometry>;
  using MeshPtr = std::shared_ptr<Mesh>;

  namespace
  {
    // Lowercased copy, so extension dispatch is case-insensitive (.STEP == .step).
    std::string to_lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      return s;
    }

    bool ends_with(const std::string& s, const std::string& suffix)
    {
      return s.size() >= suffix.size() &&
             s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
  }

  void ExportJuliaOCC(jlcxx::Module& mod)
  {
    mod.add_type<OCCGeometry>("OCCGeometry");

    // Load an OCC geometry, dispatching on the file extension exactly as the
    // Python OCCGeometry(filename) constructor does. The LoadOCC_* loaders
    // return a fully set-up OCCGeometry* (BuildFMap/CalcBoundingBox done); we
    // adopt it into a shared_ptr (the holder Python also uses).
    mod.method("load_occ_geometry", [](const std::string& filename) -> OCCPtr {
      const std::string ext = to_lower(filename);
      OCCPtr geo;
      if (ends_with(ext, ".step") || ends_with(ext, ".stp"))
        geo.reset(netgen::LoadOCC_STEP(filename));
      else if (ends_with(ext, ".brep"))
        geo.reset(netgen::LoadOCC_BREP(filename));
      else if (ends_with(ext, ".iges") || ends_with(ext, ".igs"))
        geo.reset(netgen::LoadOCC_IGES(filename));
      else
        throw std::runtime_error(
            "Unsupported OCC file '" + filename +
            "' (expected .step/.stp/.brep/.iges/.igs, case-insensitive)");
      return geo;
    });

    // generate_mesh overload for OCCGeometry. CxxWrap dispatches on the geometry
    // type, so this coexists with the CSGeometry generate_mesh. GenerateMesh is
    // the (inherited) NetgenGeometry method, dispatched through OCCGeometry's
    // exported vtable; it allocates and fills the mesh.
    //
    // Unlike the CSG path (which passes the geometry into CSGGenerateMesh), the
    // OCC surface mesher retrieves the geometry from mesh->GetGeometry() and
    // dynamic_casts it to OCCGeometry&, so we must attach it first; otherwise
    // meshing throws std::bad_cast. We pre-create the mesh to set its geometry.
    mod.method("generate_mesh", [](const OCCPtr& geo, MeshingParameters& mp) -> MeshPtr {
      auto mesh = std::make_shared<Mesh>();
      mesh->SetGeometry(geo);
      geo->GenerateMesh(mesh, mp);
      return mesh;
    });
  }
}
