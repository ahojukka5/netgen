#include "julia_csg.hpp"

// netgen::CSGeometry and the CSG kernel, via the umbrella header.
#include <csg.hpp>

namespace netgen_julia
{
  using netgen::CSGeometry;
  using netgen::OrthoBrick;
  using netgen::Solid;
  using netgen::Mesh;
  using netgen::MeshingParameters;
  using netgen::Point;
  using CSGPtr = std::shared_ptr<CSGeometry>;
  using MeshPtr = std::shared_ptr<Mesh>;

  namespace
  {
    // Build a CSGeometry containing a single axis-aligned box (OrthoBrick) as one
    // top-level solid, using Netgen's normal programmatic construction path:
    //
    //   * AddSurfaces(prim) registers the primitive's surfaces AND the surface ->
    //     primitive map (surf2prim) that the special-point finder needs; the
    //     CSGeometry::Load text path does not populate surf2prim, so a Load-ed
    //     geometry is not meshable from scratch.
    //   * SetSolid(name, solid) hands ownership of the Solid (and, through it, the
    //     OrthoBrick) to the geometry, so CSGeometry::~CSGeometry/Clean() frees
    //     them. The brick's surfaces are owned by the brick (the geometry does not
    //     delete primitive-owned surfaces), so there is no double free.
    //
    // OrthoBrick and Solid's constructors are exported from nglib (DLL_HEADER) for
    // exactly this kind of external binding.
    CSGPtr build_box(double x0, double y0, double z0,
                     double x1, double y1, double z1)
    {
      auto geo = std::make_shared<CSGeometry>();

      auto* brick = new OrthoBrick(Point<3>(x0, y0, z0), Point<3>(x1, y1, z1));
      auto* solid = new Solid(brick);

      geo->SetSolid("box", solid);
      geo->AddSurfaces(brick);
      geo->SetTopLevelObject(solid);
      geo->FindIdenticSurfaces(1e-8 * geo->MaxSize());
      return geo;
    }
  }

  void ExportJuliaCSG(jlcxx::Module& mod)
  {
    mod.add_type<CSGeometry>("CSGeometry");

    // Axis-aligned box geometry from two opposite corners.
    mod.method("box_geometry",
               [](double x0, double y0, double z0,
                  double x1, double y1, double z1) -> CSGPtr {
                 return build_box(x0, y0, z0, x1, y1, z1);
               });

    // Convenience: the unit cube [0,1]^3.
    mod.method("unit_cube_geometry", []() -> CSGPtr {
      return build_box(0, 0, 0, 1, 1, 1);
    });

    mod.method("num_top_level_objects", [](const CSGPtr& g) {
      return static_cast<int64_t>(g->GetNTopLevelObjects());
    });

    // Generate a volume mesh from the geometry. CSGeometry::GenerateMesh
    // allocates and fills the mesh; we return it as a shared_ptr<Mesh> so it
    // plugs straight into the Sprint 2/3 Mesh API (counts, extraction, save).
    mod.method("generate_mesh", [](const CSGPtr& geo, MeshingParameters& mp) -> MeshPtr {
      MeshPtr mesh;
      geo->GenerateMesh(mesh, mp);
      return mesh;
    });
  }
}
