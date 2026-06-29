#include "julia_csg.hpp"

// netgen::CSGeometry and the CSG kernel, via the umbrella header.
#include <csg.hpp>

namespace netgen_julia
{
  using netgen::CSGeometry;
  using netgen::OrthoBrick;
  using netgen::Sphere;
  using netgen::Solid;
  using netgen::Primitive;
  using netgen::Mesh;
  using netgen::MeshingParameters;
  using netgen::Point;
  using CSGPtr = std::shared_ptr<CSGeometry>;
  using MeshPtr = std::shared_ptr<Mesh>;

  namespace
  {
    // Wrap a single primitive as one top-level solid in a fresh CSGeometry, using
    // Netgen's normal programmatic construction path:
    //
    //   * AddSurfaces(prim) registers the primitive's surfaces AND the surface ->
    //     primitive map (surf2prim) that the special-point finder needs; the
    //     CSGeometry::Load text path does not populate surf2prim, so a Load-ed
    //     geometry is not meshable from scratch.
    //   * SetSolid(name, solid) hands ownership of the Solid (and, through it, the
    //     primitive) to the geometry, so CSGeometry::~CSGeometry/Clean() frees
    //     them. The primitive's surfaces are owned by the primitive (the geometry
    //     does not delete primitive-owned surfaces), so there is no double free.
    //
    // OrthoBrick / Sphere / Solid constructors are exported from nglib
    // (DLL_HEADER) for exactly this kind of external binding.
    CSGPtr build_from_primitive(Primitive* prim, const char* name)
    {
      auto geo = std::make_shared<CSGeometry>();
      auto* solid = new Solid(prim);
      geo->SetSolid(name, solid);
      geo->AddSurfaces(prim);
      geo->SetTopLevelObject(solid);
      geo->FindIdenticSurfaces(1e-8 * geo->MaxSize());
      return geo;
    }

    CSGPtr build_box(double x0, double y0, double z0,
                     double x1, double y1, double z1)
    {
      return build_from_primitive(
          new OrthoBrick(Point<3>(x0, y0, z0), Point<3>(x1, y1, z1)), "box");
    }

    CSGPtr build_sphere(double cx, double cy, double cz, double r)
    {
      return build_from_primitive(new Sphere(Point<3>(cx, cy, cz), r), "sphere");
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

    // Sphere geometry (curved boundary), to exercise geometry-aware refinement:
    // refining a sphere mesh projects new boundary points onto the true sphere.
    mod.method("sphere_geometry",
               [](double cx, double cy, double cz, double r) -> CSGPtr {
                 return build_sphere(cx, cy, cz, r);
               });

    mod.method("num_top_level_objects", [](const CSGPtr& g) {
      return static_cast<int64_t>(g->GetNTopLevelObjects());
    });

    // Generate a volume mesh from the geometry. CSGeometry::GenerateMesh
    // allocates and fills the mesh; we return it as a shared_ptr<Mesh> so it
    // plugs straight into the Sprint 2/3 Mesh API (counts, extraction, save).
    mod.method("generate_mesh", [](const CSGPtr& geo, MeshingParameters& mp) -> MeshPtr {
      // Attach the geometry to the mesh so later uniform_refine! uses the
      // geometry-aware refinement path (CSGeometry projects new boundary points
      // onto its surfaces). CSGGenerateMesh meshes from the geometry passed in
      // and keeps the attached geometry (DeleteMesh does not clear it).
      auto mesh = std::make_shared<Mesh>();
      mesh->SetGeometry(geo);
      geo->GenerateMesh(mesh, mp);
      mesh->SetGeometry(geo);
      mesh->geomtype = Mesh::GEOM_CSG;
      return mesh;
    });
  }
}
