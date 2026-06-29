#include "julia_topology.hpp"

#include <algorithm>

#include <jlcxx/array.hpp>

// netgen::Mesh and netgen::MeshTopology (libsrc/meshing/topology.hpp, reached via
// the meshing umbrella). All accessors used here are inline header methods or the
// already-exported Mesh::UpdateTopology / Mesh::GetNDomains; no new exports.
#include <meshing.hpp>

namespace netgen_julia
{
  using netgen::Mesh;
  using netgen::MeshTopology;
  using netgen::Element;
  using netgen::ELEMENT_TYPE;
  using netgen::ELEMENT_EDGE;
  using netgen::PointIndex;
  using MeshPtr = std::shared_ptr<Mesh>;

  namespace
  {
    // Ensure the mesh topology is current. Mesh::UpdateTopology() rebuilds only
    // when the mesh changed (timestamp guard), so calling it per query is cheap
    // and means callers never need an explicit build step.
    const MeshTopology& topo(const MeshPtr& m)
    {
      m->UpdateTopology();
      return m->GetTopology();
    }

    // Maximum local edge/face count over all volume elements (the dense stride).
    template <typename GetLocal>
    int volume_stride(const MeshPtr& m, GetLocal get_local)
    {
      int s = 0;
      const auto& top = m->GetTopology();
      for (auto ei : m->VolumeElements().Range())
        s = std::max(s, static_cast<int>(get_local(top, ei).Size()));
      return s;
    }
  }

  void ExportJuliaTopology(jlcxx::Module& mod)
  {
    // Explicit topology build (optional; every query below builds lazily anyway).
    mod.method("build_topology!", [](const MeshPtr& m) { m->UpdateTopology(); });

    // Global counts.
    mod.method("num_edges", [](const MeshPtr& m) {
      return static_cast<int64_t>(topo(m).GetNEdges());
    });
    mod.method("num_faces", [](const MeshPtr& m) {
      return static_cast<int64_t>(topo(m).GetNFaces());
    });

    // Global edges: flat [a1,b1, a2,b2, ...]; reshape to 2 x num_edges. Endpoints
    // are 1-based point ids, returned in Netgen's stored (canonical, ascending)
    // order. Edge id in Julia is the column index (1-based).
    mod.method("edge_connectivity_flat", [](const MeshPtr& m) {
      const auto& top = topo(m);
      jlcxx::Array<int64_t> out;
      const int ne = static_cast<int>(top.GetNEdges());
      for (int e = 0; e < ne; e++)
      {
        auto ev = top.GetEdgeVertices(e);  // std::array<PointIndex,2>
        out.push_back(static_cast<int64_t>(ev[0]));
        out.push_back(static_cast<int64_t>(ev[1]));
      }
      return out;
    });

    // Global faces: flat, stride x num_faces, 0-padded, 1-based point ids. Stride
    // is the max face vertex count (3 for all-triangle meshes, 4 if any quad).
    mod.method("face_connectivity_flat", [](const MeshPtr& m) {
      const auto& top = topo(m);
      const int nf = static_cast<int>(top.GetNFaces());
      int stride = 3;
      for (int f = 0; f < nf; f++)
        if (top.GetFaceVertices(f).Size() == 4) { stride = 4; break; }
      jlcxx::Array<int64_t> out;
      for (int f = 0; f < nf; f++)
      {
        auto fv = top.GetFaceVertices(f);  // FlatArray<PointIndex>, 3 or 4
        for (int k = 0; k < stride; k++)
          out.push_back(k < fv.Size() ? static_cast<int64_t>(fv[k]) : int64_t(0));
      }
      return out;
    });

    // Volume-element -> global edge ids: flat, stride x num_volume_elements,
    // 0-padded, 1-based. For pure-tet meshes stride is 6.
    mod.method("volume_to_edge_flat", [](const MeshPtr& m) {
      const auto& top = topo(m);
      const int stride = volume_stride(m, [](const MeshTopology& t, auto ei) {
        return t.GetEdges(ei);
      });
      jlcxx::Array<int64_t> out;
      for (auto ei : m->VolumeElements().Range())
      {
        auto ge = top.GetEdges(ei);  // FlatArray<EdgeIndex> (0-based)
        for (int k = 0; k < stride; k++)
          out.push_back(k < ge.Size() ? static_cast<int64_t>(ge[k]) + 1 : int64_t(0));
      }
      return out;
    });

    // Volume-element -> global face ids: flat, stride x num_volume_elements,
    // 0-padded, 1-based. For pure-tet meshes stride is 4.
    mod.method("volume_to_face_flat", [](const MeshPtr& m) {
      const auto& top = topo(m);
      const int stride = volume_stride(m, [](const MeshTopology& t, auto ei) {
        return t.GetFaces(ei);
      });
      jlcxx::Array<int64_t> out;
      for (auto ei : m->VolumeElements().Range())
      {
        auto gf = top.GetFaces(ei);  // FlatArray<FaceIndex> (0-based)
        for (int k = 0; k < stride; k++)
          out.push_back(k < gf.Size() ? static_cast<int64_t>(gf[k]) + 1 : int64_t(0));
      }
      return out;
    });

    // Edge orientation signs per local volume-element edge: flat, stride x
    // num_volume_elements (same layout as volume_to_edge_flat), values in
    // {+1,-1} (0 in padding). Convention: +1 if the element's local edge
    // direction (from its connectivity, in Netgen's local edge order) agrees
    // with the canonical global edge direction (edge_connectivity), else -1.
    mod.method("volume_to_edge_orientation_flat", [](const MeshPtr& m) {
      const auto& top = topo(m);
      const int stride = volume_stride(m, [](const MeshTopology& t, auto ei) {
        return t.GetEdges(ei);
      });
      jlcxx::Array<int32_t> out;
      for (auto ei : m->VolumeElements().Range())
      {
        const Element& el = (*m)[ei];
        const ELEMENT_EDGE* led = MeshTopology::GetEdges1(el.GetType());  // 1-based local
        auto ge = top.GetEdges(ei);
        const int n = ge.Size();
        for (int k = 0; k < stride; k++)
        {
          if (k >= n) { out.push_back(0); continue; }
          PointIndex pa = el[led[k][0] - 1];  // operator[] is 0-based
          PointIndex pb = el[led[k][1] - 1];
          auto gv = top.GetEdgeVertices(static_cast<int>(ge[k]));
          int32_t sign = (static_cast<int>(pa) == static_cast<int>(gv[0]) &&
                          static_cast<int>(pb) == static_cast<int>(gv[1])) ? 1 : -1;
          out.push_back(sign);
        }
      }
      return out;
    });

    // Surface-element -> global volume-face id, 1-based; length
    // num_surface_elements.
    mod.method("surface_to_face_flat", [](const MeshPtr& m) {
      const auto& top = topo(m);
      jlcxx::Array<int64_t> out;
      for (auto si : m->SurfaceElements().Range())
        out.push_back(static_cast<int64_t>(top.GetFace(si)) + 1);
      return out;
    });

    // Number of sub-domains (materials); pairs with volume_element_indices.
    mod.method("num_domains", [](const MeshPtr& m) {
      return static_cast<int64_t>(m->GetNDomains());
    });
  }
}
