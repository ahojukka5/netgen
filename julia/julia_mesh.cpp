#include "julia_mesh.hpp"

#include <jlcxx/array.hpp>

// netgen::Mesh and its count/IO members (libsrc/meshing/meshclass.hpp), pulled
// in via the same umbrella header the Python binding uses.
#include <meshing.hpp>

namespace netgen_julia
{
  using netgen::Mesh;
  using netgen::Point3d;
  using netgen::Element;
  using netgen::Element2d;
  using netgen::Segment;
  using netgen::ELEMENT_TYPE;
  using netgen::PointIndex;
  using MeshPtr = std::shared_ptr<Mesh>;

  namespace
  {
    // Build a flat, column-major, 0-padded connectivity array for a homogeneous
    // stride. `get_nv(el)` returns the number of corner vertices, `vert(el, j)`
    // the j-th (0-based) corner vertex as a 1-based PointIndex value. Layout is
    // stride x nelem: each element is one contiguous column of length `stride`,
    // its first get_nv entries are the (1-based) node indices, the rest are 0.
    // Reshape in Julia with reshape(v, stride, nelem); stride = length / nelem.
    template <typename Container, typename GetNV, typename Vert>
    jlcxx::Array<int64_t> pack_connectivity(const Container& els, int stride,
                                            GetNV get_nv, Vert vert)
    {
      jlcxx::Array<int64_t> conn;
      for (const auto& el : els)
      {
        int nv = get_nv(el);
        for (int j = 0; j < stride; j++)
          conn.push_back(j < nv ? static_cast<int64_t>(vert(el, j)) : int64_t(0));
      }
      return conn;
    }

    // Maximum corner-vertex count over a container of elements (0 if empty).
    template <typename Container, typename GetNV>
    int max_nv(const Container& els, GetNV get_nv)
    {
      int s = 0;
      for (const auto& el : els) s = std::max(s, int(get_nv(el)));
      return s;
    }
  }

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

    // --- read-only bulk content extraction ---------------------------------
    // All returns are copies into native Julia arrays (jlcxx::Array). Index
    // conventions exposed to Julia:
    //   * point order matches Netgen's 1-based PointIndex,
    //   * connectivity values are 1-based node indices (Netgen PointIndex BASE=1
    //     is preserved as-is) and 0 means "no node" (padding),
    //   * only corner vertices (GetNV) are extracted; high-order / curved nodes
    //     are deliberately deferred.

    // Coordinates, flat point-major: [x1,y1,z1, x2,y2,z2, ...].
    // Julia: reshape(point_coordinates_flat(m), 3, num_points(m)) -> 3 x np.
    mod.method("point_coordinates_flat", [](const MeshPtr& m) {
      jlcxx::Array<double> coords;
      for (const auto& mp : m->Points())
      {
        coords.push_back(mp(0));
        coords.push_back(mp(1));
        coords.push_back(mp(2));
      }
      return coords;
    });

    // Volume elements: connectivity (flat, stride x nelem, 0-padded, 1-based)
    // and ELEMENT_TYPE enum ids (TET=20, PRISM=23, PYRAMID=22, HEX=25, ...).
    mod.method("volume_connectivity_flat", [](const MeshPtr& m) {
      const auto& els = m->VolumeElements();
      int stride = max_nv(els, [](const Element& e) { return e.GetNV(); });
      return pack_connectivity(els, stride,
                               [](const Element& e) { return e.GetNV(); },
                               [](const Element& e, int j) { return e[j]; });
    });
    mod.method("volume_element_types", [](const MeshPtr& m) {
      jlcxx::Array<int32_t> types;
      for (const auto& el : m->VolumeElements())
        types.push_back(static_cast<int32_t>(el.GetType()));
      return types;
    });
    // Per-volume-element sub-domain / material index (Element::GetIndex()),
    // complementing surface_element_indices (the face-descriptor index).
    mod.method("volume_element_indices", [](const MeshPtr& m) {
      jlcxx::Array<int32_t> idx;
      for (const auto& el : m->VolumeElements())
        idx.push_back(static_cast<int32_t>(el.GetIndex()));
      return idx;
    });

    // Surface elements: connectivity, ELEMENT_TYPE ids (TRIG=10, QUAD=11, ...)
    // and the face-descriptor index per element (Element2d::GetIndex()).
    mod.method("surface_connectivity_flat", [](const MeshPtr& m) {
      const auto& els = m->SurfaceElements();
      int stride = max_nv(els, [](const Element2d& e) { return e.GetNV(); });
      return pack_connectivity(els, stride,
                               [](const Element2d& e) { return e.GetNV(); },
                               [](const Element2d& e, int j) { return e[j]; });
    });
    mod.method("surface_element_types", [](const MeshPtr& m) {
      jlcxx::Array<int32_t> types;
      for (const auto& el : m->SurfaceElements())
        types.push_back(static_cast<int32_t>(el.GetType()));
      return types;
    });
    mod.method("surface_element_indices", [](const MeshPtr& m) {
      jlcxx::Array<int32_t> idx;
      for (const auto& el : m->SurfaceElements())
        idx.push_back(static_cast<int32_t>(el.GetIndex()));
      return idx;
    });

    // Segments (1D): corner connectivity (stride 2, 1-based) and ELEMENT_TYPE
    // ids (SEGMENT=1, SEGMENT3=2). SEGMENT3 mid-nodes are not extracted.
    mod.method("segment_connectivity_flat", [](const MeshPtr& m) {
      const auto& segs = m->LineSegments();
      return pack_connectivity(segs, /*stride=*/2,
                               [](const Segment&) { return 2; },
                               [](const Segment& s, int j) { return s[j]; });
    });
    mod.method("segment_element_types", [](const MeshPtr& m) {
      jlcxx::Array<int32_t> types;
      for (const auto& s : m->LineSegments())
        types.push_back(static_cast<int32_t>(s.GetType()));
      return types;
    });

    // Map an ELEMENT_TYPE enum id to a short name (for diagnostics/tests).
    mod.method("element_type_name", [](int32_t id) -> std::string {
      switch (static_cast<ELEMENT_TYPE>(id))
      {
        case netgen::SEGMENT:   return "SEGMENT";
        case netgen::SEGMENT3:  return "SEGMENT3";
        case netgen::TRIG:      return "TRIG";
        case netgen::QUAD:      return "QUAD";
        case netgen::TRIG6:     return "TRIG6";
        case netgen::QUAD6:     return "QUAD6";
        case netgen::QUAD8:     return "QUAD8";
        case netgen::TET:       return "TET";
        case netgen::TET10:     return "TET10";
        case netgen::PYRAMID:   return "PYRAMID";
        case netgen::PRISM:     return "PRISM";
        case netgen::PRISM12:   return "PRISM12";
        case netgen::PRISM15:   return "PRISM15";
        case netgen::PYRAMID13: return "PYRAMID13";
        case netgen::HEX:       return "HEX";
        case netgen::HEX20:     return "HEX20";
        case netgen::HEX7:      return "HEX7";
        default:                return "UNKNOWN";
      }
    });

    // --- deterministic tiny mesh for smoke tests ---------------------------
    // Single unit tetrahedron with its four triangular boundary faces. Mirrors
    // the way nglib builds meshes (FaceDescriptor + 1-based PNum), so it stays a
    // valid, saveable .vol. Intended for tests only.
    mod.method("make_unit_tet_mesh", []() -> MeshPtr {
      auto m = std::make_shared<Mesh>();
      m->AddFaceDescriptor(netgen::FaceDescriptor(1, 1, 0, 1));

      netgen::PointIndex p1 = m->AddPoint(Point3d(0, 0, 0));
      netgen::PointIndex p2 = m->AddPoint(Point3d(1, 0, 0));
      netgen::PointIndex p3 = m->AddPoint(Point3d(0, 1, 0));
      netgen::PointIndex p4 = m->AddPoint(Point3d(0, 0, 1));

      Element tet(netgen::TET);
      tet.SetIndex(1);
      tet.PNum(1) = p1; tet.PNum(2) = p2; tet.PNum(3) = p3; tet.PNum(4) = p4;
      m->AddVolumeElement(tet);

      const int faces[4][3] = {{1, 2, 3}, {1, 2, 4}, {1, 3, 4}, {2, 3, 4}};
      const netgen::PointIndex pts[4] = {p1, p2, p3, p4};
      for (auto& f : faces)
      {
        Element2d tri(netgen::TRIG);
        tri.SetIndex(1);
        tri.PNum(1) = pts[f[0] - 1];
        tri.PNum(2) = pts[f[1] - 1];
        tri.PNum(3) = pts[f[2] - 1];
        m->AddSurfaceElement(tri);
      }
      return m;
    });

    // --- refinement / hierarchy --------------------------------------------
    // Deep copy of a mesh (Mesh::operator=). Lets callers build a hierarchy of
    // distinct meshes by copying then refining, since uniform_refine! mutates in
    // place.
    mod.method("copy_mesh", [](const MeshPtr& m) -> MeshPtr {
      auto c = std::make_shared<Mesh>();
      *c = *m;                    // copies points/elements and the geometry handle
      c->geomtype = m->geomtype;  // operator= does not copy the geomtype tag
      return c;
    });

    // In-place uniform refinement. Uses the mesh's geometry refinement, which
    // projects new boundary points onto the true geometry when one is attached
    // (CSG/OCC generated meshes), or a default refinement otherwise
    // (Mesh::GetGeometry() never returns null). Netgen records the refinement
    // hierarchy on the mesh (public mlbetweennodes / mlparentelement /
    // mlparentsurfaceelement arrays) for possible later extraction.
    mod.method("uniform_refine!", [](const MeshPtr& m) {
      m->GetGeometry()->GetRefinement().Refine(*m);
    });
    // NOTE: a uniform_refinement_hierarchy returning std::vector<shared_ptr<Mesh>>
    // was considered but not added: CxxWrap has no factory for that container
    // (it breaks module registration) and registering one is more ceremony than
    // it is worth. The generic idiom is to build levels explicitly in Julia:
    //   levels = Any[m0]
    //   for _ in 1:n; m = copy_mesh(levels[end]); uniform_refine!(m); push!(levels, m); end
    // copy_mesh preserves the geometry handle and geomtype, so every level stays
    // geometry-backed.

    // --- geometry diagnostics ----------------------------------------------
    // Broad kind of geometry the mesh was generated from. Mesh::GetGeometry()
    // never returns null (it falls back to a default geometry), so we report the
    // mesh's own geomtype tag instead: "csg", "occ", "2d", "stl", "acis", or
    // "none" (default / no explicit geometry, e.g. new_mesh / load_mesh).
    mod.method("geometry_type_name", [](const MeshPtr& m) -> std::string {
      switch (m->geomtype)
      {
        case Mesh::GEOM_2D:   return "2d";
        case Mesh::GEOM_CSG:  return "csg";
        case Mesh::GEOM_STL:  return "stl";
        case Mesh::GEOM_OCC:  return "occ";
        case Mesh::GEOM_ACIS: return "acis";
        default:              return "none";
      }
    });
    mod.method("has_geometry", [](const MeshPtr& m) {
      return m->geomtype != Mesh::NO_GEOM;
    });

    // --- refinement hierarchy / parent maps --------------------------------
    // Point ancestry from uniform refinement (Mesh::mlbetweennodes). Flat layout
    // [a1,b1, a2,b2, ...]; reshape to 2 x num_points. Conventions:
    //   * ids are 1-based and index points of the immediately coarser level,
    //   * a coarse/original point p is reported as [p, p],
    //   * a new midpoint between coarse points a,b is [a, b] with a != b.
    // Netgen marks originals with invalid sentinels internally; we normalize
    // those to [p, p] so no C++ sentinel leaks to Julia. Before any refinement
    // the array is empty, so every point maps to itself.
    mod.method("point_parent_vertices_flat", [](const MeshPtr& m) {
      jlcxx::Array<int64_t> out;
      const auto& mbn = m->mlbetweennodes;
      const bool have = mbn.Size() >= static_cast<size_t>(m->GetNP());
      for (PointIndex pi : m->Points().Range())
      {
        int64_t self = static_cast<int64_t>(pi);  // 1-based (PointIndex BASE=1)
        if (have)
        {
          auto pr = mbn[pi];
          if (pr.I1().IsValid() && pr.I2().IsValid())
          {
            out.push_back(static_cast<int64_t>(pr.I1()));
            out.push_back(static_cast<int64_t>(pr.I2()));
            continue;
          }
        }
        out.push_back(self);
        out.push_back(self);
      }
      return out;
    });

    // Parent volume-element id per element (Mesh::mlparentelement), 1-based.
    // NOTE: Netgen populates this only on the bisection refinement path; uniform
    // refinement does not record element ancestry, so after uniform_refine! the
    // array is empty and every element maps to itself (identity / root). Always
    // length num_volume_elements, values in 1:num_volume_elements.
    mod.method("volume_parent_indices", [](const MeshPtr& m) {
      jlcxx::Array<int64_t> out;
      const auto& mpe = m->mlparentelement;
      const bool have = mpe.Size() >= static_cast<size_t>(m->GetNE());
      int64_t e = 1;
      for (auto ei : m->VolumeElements().Range())
      {
        if (have && mpe[ei].IsValid())
          out.push_back(static_cast<int64_t>(mpe[ei]) + 1);  // 0-based -> 1-based
        else
          out.push_back(e);
        ++e;
      }
      return out;
    });

    // Parent surface-element id per surface element (Mesh::mlparentsurfaceelement),
    // 1-based; same bisection-only caveat and identity fallback as the volume map.
    mod.method("surface_parent_indices", [](const MeshPtr& m) {
      jlcxx::Array<int64_t> out;
      const auto& mps = m->mlparentsurfaceelement;
      const bool have = mps.Size() >= static_cast<size_t>(m->GetNSE());
      int64_t e = 1;
      for (auto si : m->SurfaceElements().Range())
      {
        if (have && mps[si].IsValid())
          out.push_back(static_cast<int64_t>(mps[si]) + 1);
        else
          out.push_back(e);
        ++e;
      }
      return out;
    });

    // Vertex count at each refinement level (Mesh::level_nv): level_vertex_counts
    // [k] is the number of vertices at level k (k=0 coarse). Empty before any
    // refinement; length nlevels+1 after nlevels uniform refinements.
    mod.method("level_vertex_counts", [](const MeshPtr& m) {
      jlcxx::Array<int64_t> out;
      for (size_t i = 0; i < m->level_nv.Size(); i++)
        out.push_back(static_cast<int64_t>(m->level_nv[i]));
      return out;
    });
  }
}
