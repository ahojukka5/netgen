# Minimal Julia smoke test for the experimental Netgen Julia binding (USE_JULIA=ON).
#
# Run it from the build tree, pointing it at the freshly built libngjl, e.g.:
#
#   julia julia/smoke.jl <build_dir>/julia/libngjl.dylib
#
# or:
#
#   NGJL_LIB=<build_dir>/julia/libngjl.dylib julia julia/smoke.jl
#
# The library path is intentionally not hardcoded so the test stays portable.

using CxxWrap

const libngjl = !isempty(get(ENV, "NGJL_LIB", "")) ? ENV["NGJL_LIB"] :
                length(ARGS) >= 1 ? ARGS[1] : ""

if isempty(libngjl)
    error("""
          Path to the built libngjl was not provided.
          Pass it as the first argument or via the NGJL_LIB environment variable:
              julia julia/smoke.jl <build_dir>/julia/libngjl.dylib
          """)
end

if !isfile(libngjl)
    error("libngjl not found at: $libngjl")
end

module NetgenJL
    using CxxWrap
    @wrapmodule(() -> Main.libngjl)
    function __init__()
        @initcxx
    end
end

# --- Sprint 0: trivial smoke functions -------------------------------------
@assert NetgenJL.netgen_julia_smoke() == 42
@assert NetgenJL.netgen_julia_hello() == "netgen-julia-ok"

# --- Sprint 1: netgen::Point3d ---------------------------------------------
p = NetgenJL.Point3d(1.0, 2.0, 3.0)
@assert NetgenJL.x(p) == 1.0
@assert NetgenJL.y(p) == 2.0
@assert NetgenJL.z(p) == 3.0

# --- Sprint 1: netgen::Vec3d -----------------------------------------------
v = NetgenJL.Vec3d(3.0, 0.0, 4.0)
@assert NetgenJL.x(v) == 3.0
@assert NetgenJL.y(v) == 0.0
@assert NetgenJL.z(v) == 4.0
@assert NetgenJL.length(v) == 5.0

# --- Sprint 1: netgen::MeshingParameters -----------------------------------
mp = NetgenJL.MeshingParameters()
NetgenJL.set_maxh!(mp, 0.25)
@assert NetgenJL.maxh(mp) == 0.25
NetgenJL.set_grading!(mp, 0.4)
@assert NetgenJL.grading(mp) == 0.4
@assert NetgenJL.secondorder(mp) == false
NetgenJL.set_secondorder!(mp, true)
@assert NetgenJL.secondorder(mp) == true

# --- Sprint 2: netgen::Mesh handle (shared_ptr), counts, save/load ----------
m = NetgenJL.new_mesh()
@assert NetgenJL.num_points(m) == 0
@assert NetgenJL.num_volume_elements(m) == 0
@assert NetgenJL.num_surface_elements(m) == 0
@assert NetgenJL.num_segments(m) == 0

# Save/load roundtrip of the (empty) mesh through a temporary .vol file.
tmp = tempname() * ".vol"
NetgenJL.save_mesh(m, tmp)
@assert isfile(tmp)
m2 = NetgenJL.load_mesh(tmp)
@assert NetgenJL.num_points(m2) == NetgenJL.num_points(m)
@assert NetgenJL.num_volume_elements(m2) == NetgenJL.num_volume_elements(m)
@assert NetgenJL.num_surface_elements(m2) == NetgenJL.num_surface_elements(m)
rm(tmp; force=true)

# Empty-mesh bulk extraction returns consistent empty arrays.
@assert length(NetgenJL.point_coordinates_flat(m)) == 0
@assert length(NetgenJL.volume_connectivity_flat(m)) == 0
@assert length(NetgenJL.surface_connectivity_flat(m)) == 0
@assert length(NetgenJL.volume_element_types(m)) == 0

# --- Sprint 3: read-only content extraction from a non-empty mesh -----------
# Deterministic single unit tetrahedron (4 points, 1 tet, 4 triangular faces).
# Connectivity returned to Julia is 1-based (Netgen PointIndex BASE=1 preserved);
# 0 is padding. Coordinates are flat point-major; reshape to 3 x np.
tet = NetgenJL.make_unit_tet_mesh()
@assert NetgenJL.num_points(tet) == 4
@assert NetgenJL.num_volume_elements(tet) == 1
@assert NetgenJL.num_surface_elements(tet) == 4

# Coordinates: 3 x 4 matrix (column j = point j).
coords = reshape(NetgenJL.point_coordinates_flat(tet), 3, 4)
@assert coords isa Matrix{Float64}
@assert coords[:, 1] == [0.0, 0.0, 0.0]
@assert coords[:, 2] == [1.0, 0.0, 0.0]
@assert coords[:, 3] == [0.0, 1.0, 0.0]
@assert coords[:, 4] == [0.0, 0.0, 1.0]

# Volume connectivity: stride x ne, here 4 x 1; one TET with nodes 1,2,3,4.
vconn = reshape(NetgenJL.volume_connectivity_flat(tet), :, 1)
vtypes = NetgenJL.volume_element_types(tet)
@assert vec(vconn) == [1, 2, 3, 4]
@assert collect(vtypes) == Int32[20]                 # 20 == TET
@assert NetgenJL.element_type_name(vtypes[1]) == "TET"

# Surface connectivity: stride x nse, here 3 x 4; four TRIG faces, face index 1.
sconn = reshape(NetgenJL.surface_connectivity_flat(tet), :, 4)
stypes = NetgenJL.surface_element_types(tet)
sidx = NetgenJL.surface_element_indices(tet)
@assert size(sconn) == (3, 4)
@assert all(==(10), collect(stypes))                 # 10 == TRIG
@assert all(==(1), collect(sidx))
@assert NetgenJL.element_type_name(stypes[1]) == "TRIG"
@assert all(1 .<= vec(sconn) .<= 4)                  # all node indices in 1..np

# Save/load roundtrip must preserve extracted contents.
tmp2 = tempname() * ".vol"
NetgenJL.save_mesh(tet, tmp2)
tet2 = NetgenJL.load_mesh(tmp2)
@assert NetgenJL.num_points(tet2) == 4
@assert reshape(NetgenJL.point_coordinates_flat(tet2), 3, 4) == coords
@assert reshape(NetgenJL.volume_connectivity_flat(tet2), :, 1) == vconn
@assert reshape(NetgenJL.surface_connectivity_flat(tet2), :, 4) == sconn
rm(tmp2; force=true)

# --- Sprint 4: CSG geometry + mesh generation ------------------------------
# Mesh the unit cube via a CSG OrthoBrick. Counts depend on maxh, so we assert
# structural properties (positive counts, element types, bounding box) rather
# than exact numbers.
geo = NetgenJL.unit_cube_geometry()
@assert NetgenJL.num_top_level_objects(geo) == 1

genmp = NetgenJL.MeshingParameters()
NetgenJL.set_maxh!(genmp, 0.5)
gen = NetgenJL.generate_mesh(geo, genmp)
gnp = NetgenJL.num_points(gen)
gne = NetgenJL.num_volume_elements(gen)
gnse = NetgenJL.num_surface_elements(gen)
@assert gnp > 4
@assert gne > 0
@assert gnse > 0

# Volume is all tetrahedra, boundary all triangles.
@assert all(==(20), collect(NetgenJL.volume_element_types(gen)))   # 20 == TET
@assert all(==(10), collect(NetgenJL.surface_element_types(gen)))  # 10 == TRIG

# Generated coordinates lie within the unit cube.
gcoords = reshape(NetgenJL.point_coordinates_flat(gen), 3, gnp)
@assert all(-1e-9 .<= gcoords .<= 1 + 1e-9)

# Finer maxh yields at least as many points (sanity of parameter plumbing).
genmp2 = NetgenJL.MeshingParameters()
NetgenJL.set_maxh!(genmp2, 0.25)
gen2 = NetgenJL.generate_mesh(geo, genmp2)
@assert NetgenJL.num_points(gen2) >= gnp

# Generated mesh survives a save/load roundtrip with identical counts.
gtmp = tempname() * ".vol"
NetgenJL.save_mesh(gen, gtmp)
genrt = NetgenJL.load_mesh(gtmp)
@assert NetgenJL.num_points(genrt) == gnp
@assert NetgenJL.num_volume_elements(genrt) == gne
@assert NetgenJL.num_surface_elements(genrt) == gnse
rm(gtmp; force=true)

# --- Sprint 6: uniform refinement, copy, after-refine extraction ------------
r0 = NetgenJL.generate_mesh(geo, genmp)          # fresh unit-cube mesh (maxh 0.5)
rnp0 = NetgenJL.num_points(r0)
rne0 = NetgenJL.num_volume_elements(r0)

# copy_mesh is a deep copy: refining the copy leaves the original untouched.
rcopy = NetgenJL.copy_mesh(r0)
NetgenJL.uniform_refine!(rcopy)
@assert NetgenJL.num_points(r0) == rnp0
@assert NetgenJL.num_volume_elements(rcopy) > rne0

# in-place uniform refinement grows the mesh.
NetgenJL.uniform_refine!(r0)
rnp1 = NetgenJL.num_points(r0)
rne1 = NetgenJL.num_volume_elements(r0)
@assert rnp1 > rnp0
@assert rne1 > rne0

# extraction still works (and stays consistent) after refinement.
rcoords = NetgenJL.point_coordinates_flat(r0)
@assert length(rcoords) == 3 * rnp1
@assert length(NetgenJL.volume_connectivity_flat(r0)) > 0
@assert all(==(20), collect(NetgenJL.volume_element_types(r0)))      # still TET
@assert length(NetgenJL.volume_element_indices(r0)) == rne1
@assert all(-1e-9 .<= reshape(rcoords, 3, rnp1) .<= 1 + 1e-9)        # still in cube

# save/load roundtrip works after refinement.
rtmp = tempname() * ".vol"
NetgenJL.save_mesh(r0, rtmp)
r0b = NetgenJL.load_mesh(rtmp)
@assert NetgenJL.num_points(r0b) == rnp1
@assert NetgenJL.num_volume_elements(r0b) == rne1
rm(rtmp; force=true)

# second refinement level (explicit hierarchy step).
NetgenJL.uniform_refine!(r0)
rne2 = NetgenJL.num_volume_elements(r0)
@assert rne2 > rne1

# --- Sprint 7: geometry diagnostics + refinement parent maps ----------------
# Geometry-backed mesh reports its broad geometry kind; copy_mesh preserves it.
h0 = NetgenJL.generate_mesh(geo, genmp)              # fresh unit-cube mesh
@assert NetgenJL.geometry_type_name(h0) == "csg"
@assert NetgenJL.has_geometry(h0) == true
@assert isempty(collect(NetgenJL.level_vertex_counts(h0)))   # not yet refined

h1 = NetgenJL.copy_mesh(h0)
@assert NetgenJL.geometry_type_name(h1) == "csg"     # geometry tag preserved
hnp0 = NetgenJL.num_points(h0)
NetgenJL.uniform_refine!(h1)                          # geometry-aware refinement
hnp1 = NetgenJL.num_points(h1)
@assert NetgenJL.num_points(h0) == hnp0               # copy did not touch original

# Point parent pairs: 2 x np, 1-based, coarse nodes map to themselves.
parents = reshape(NetgenJL.point_parent_vertices_flat(h1), 2, hnp1)
@assert size(parents) == (2, hnp1)
@assert all(1 .<= parents .<= hnp1)
@assert all(parents[1, p] == p && parents[2, p] == p for p in 1:hnp0)   # coarse -> self
newnodes = [p for p in 1:hnp1 if parents[1, p] != parents[2, p]]
@assert !isempty(newnodes)                            # midpoints have two parents
@assert all(p > hnp0 for p in newnodes)

# level_vertex_counts after one refinement: [coarse_nv, fine_nv].
@assert collect(NetgenJL.level_vertex_counts(h1)) == [hnp0, hnp1]

# Volume/surface parent ids: length matches element count, ids in range.
# (Uniform refinement records point ancestry, not element ancestry, so these
#  fall back to identity; see julia_mesh.cpp.)
hvp = NetgenJL.volume_parent_indices(h1)
hsp = NetgenJL.surface_parent_indices(h1)
@assert length(hvp) == NetgenJL.num_volume_elements(h1)
@assert all(1 .<= hvp .<= NetgenJL.num_volume_elements(h1))
@assert length(hsp) == NetgenJL.num_surface_elements(h1)

# P1 nodal transfer sanity on flat geometry: parent pairs must reproduce a
# linear function exactly. NOT a prolongation API -- a coherence check only.
ufun(x, y, z) = 1 + 2x - 3y + 0.5z
c0 = reshape(NetgenJL.point_coordinates_flat(h0), 3, hnp0)
c1 = reshape(NetgenJL.point_coordinates_flat(h1), 3, hnp1)
u0 = [ufun(c0[:, p]...) for p in 1:hnp0]
uf = [parents[1, p] == parents[2, p] ? u0[parents[1, p]] :
      0.5 * (u0[parents[1, p]] + u0[parents[2, p]]) for p in 1:hnp1]
uexact = [ufun(c1[:, p]...) for p in 1:hnp1]
p1_err = maximum(abs.(uf .- uexact))
@assert p1_err < 1e-10

# Curved geometry: refining a sphere mesh must project new boundary points onto
# the true sphere (geometry-aware refinement), so all boundary nodes stay at
# radius r. Without projection, refined chord-midpoints would fall inside (r<1).
function boundary_radii(mesh, cx, cy, cz)
    npts = NetgenJL.num_points(mesh)
    nse = NetgenJL.num_surface_elements(mesh)
    coords = reshape(NetgenJL.point_coordinates_flat(mesh), 3, npts)
    sconn = reshape(NetgenJL.surface_connectivity_flat(mesh), :, nse)
    bnodes = filter(>(0), unique(vec(sconn)))
    [sqrt((coords[1, p] - cx)^2 + (coords[2, p] - cy)^2 + (coords[3, p] - cz)^2)
     for p in bnodes]
end
sgeo = NetgenJL.sphere_geometry(0.0, 0.0, 0.0, 1.0)
sphmp = NetgenJL.MeshingParameters(); NetgenJL.set_maxh!(sphmp, 0.5)
sm0 = NetgenJL.generate_mesh(sgeo, sphmp)
snp0 = NetgenJL.num_points(sm0)
sm1 = NetgenJL.copy_mesh(sm0)
NetgenJL.uniform_refine!(sm1)
@assert NetgenJL.num_points(sm1) > snp0
sphere_err = maximum(abs.(boundary_radii(sm1, 0.0, 0.0, 0.0) .- 1.0))
@assert sphere_err < 1e-6        # boundary nodes snapped to the sphere

# --- Sprint 8: topology / incidence -----------------------------------------
# Unit tetrahedron: 6 global edges, 4 global faces.
@assert NetgenJL.num_edges(tet) == 6
@assert NetgenJL.num_faces(tet) == 4
tedges = reshape(NetgenJL.edge_connectivity_flat(tet), 2, 6)
@assert all(tedges[1, :] .< tedges[2, :])            # canonical ascending endpoints
@assert all(1 .<= tedges .<= 4)
tfaces = reshape(NetgenJL.face_connectivity_flat(tet), :, 4)
@assert size(tfaces, 1) == 3                         # all triangular faces
@assert all(1 .<= tfaces .<= 4)
tve = reshape(NetgenJL.volume_to_edge_flat(tet), 6, 1)
tvf = reshape(NetgenJL.volume_to_face_flat(tet), 4, 1)
@assert sort(vec(tve)) == collect(1:6)               # each global edge once
@assert sort(vec(tvf)) == collect(1:4)               # each global face once
ts2f = NetgenJL.surface_to_face_flat(tet)
@assert length(ts2f) == 4 && all(1 .<= ts2f .<= 4)
@assert NetgenJL.num_domains(tet) == 1

# Rigorous edge-orientation check using Netgen's TET local edge table (1-based
# local vertex pairs, matching MeshTopology::GetEdges1(TET)).
tet_local_edges = [(4, 1), (4, 2), (4, 3), (1, 2), (1, 3), (2, 3)]
tconn = reshape(NetgenJL.volume_connectivity_flat(tet), :, 1)[:, 1]   # 4 node ids
torient = reshape(NetgenJL.volume_to_edge_orientation_flat(tet), 6, 1)
for k in 1:6
    l0, l1 = tet_local_edges[k]
    pa, pb = tconn[l0], tconn[l1]                    # element local edge endpoints
    g = tve[k]; ga, gb = tedges[1, g], tedges[2, g]  # canonical global edge
    @assert Set((pa, pb)) == Set((ga, gb))           # same edge
    @assert torient[k] == (pa == ga && pb == gb ? 1 : -1)
end

# Generated CSG cube: topology valid; global faces cover the boundary.
t0 = NetgenJL.generate_mesh(geo, genmp)
tne0 = NetgenJL.num_edges(t0); tnf0 = NetgenJL.num_faces(t0)
tnvol = NetgenJL.num_volume_elements(t0)
@assert tne0 > 0 && tnf0 > 0
@assert tnf0 >= NetgenJL.num_surface_elements(t0)
@assert length(NetgenJL.volume_to_edge_flat(t0)) == 6 * tnvol      # all tets, stride 6
@assert length(NetgenJL.volume_to_face_flat(t0)) == 4 * tnvol
@assert all(1 .<= NetgenJL.volume_to_edge_flat(t0) .<= tne0)
@assert all(1 .<= NetgenJL.volume_to_face_flat(t0) .<= tnf0)
@assert all(1 .<= NetgenJL.surface_to_face_flat(t0) .<= tnf0)

# Topology stays valid (and rebuilds) after geometry-aware refinement.
NetgenJL.uniform_refine!(t0)
@assert NetgenJL.num_edges(t0) > tne0
@assert NetgenJL.num_faces(t0) > tnf0
@assert all(1 .<= NetgenJL.volume_to_edge_flat(t0) .<= NetgenJL.num_edges(t0))

# --- Sprint 9: geometry-backed mesh hierarchy -------------------------------
# Every hierarchy level must support all generic extraction + topology APIs.
function assert_level_extractable(mesh)
    np = NetgenJL.num_points(mesh)
    ne = NetgenJL.num_volume_elements(mesh)
    nse = NetgenJL.num_surface_elements(mesh)
    @assert length(NetgenJL.point_coordinates_flat(mesh)) == 3 * np
    @assert length(NetgenJL.volume_connectivity_flat(mesh)) > 0
    @assert length(NetgenJL.volume_element_types(mesh)) == ne
    @assert length(NetgenJL.surface_connectivity_flat(mesh)) > 0
    @assert length(NetgenJL.surface_element_types(mesh)) == nse
    @assert NetgenJL.num_edges(mesh) > 0 && NetgenJL.num_faces(mesh) > 0
    @assert length(NetgenJL.edge_connectivity_flat(mesh)) == 2 * NetgenJL.num_edges(mesh)
    @assert length(NetgenJL.face_connectivity_flat(mesh)) > 0
    @assert length(NetgenJL.volume_to_edge_flat(mesh)) > 0
    @assert length(NetgenJL.volume_to_face_flat(mesh)) > 0
    @assert length(NetgenJL.volume_to_edge_orientation_flat(mesh)) ==
            length(NetgenJL.volume_to_edge_flat(mesh))
    @assert length(NetgenJL.surface_to_face_flat(mesh)) == nse
end

# Explicit, generic hierarchy idiom (copy_mesh preserves geometry + geomtype).
function build_hierarchy(m0, n)
    levels = Any[m0]
    for _ in 1:n
        mk = NetgenJL.copy_mesh(levels[end])
        NetgenJL.uniform_refine!(mk)
        push!(levels, mk)
    end
    return levels
end

function roundtrip_counts_ok(mesh)
    f = tempname() * ".vol"
    NetgenJL.save_mesh(mesh, f)
    r = NetgenJL.load_mesh(f)
    ok = NetgenJL.num_points(r) == NetgenJL.num_points(mesh) &&
         NetgenJL.num_volume_elements(r) == NetgenJL.num_volume_elements(mesh) &&
         NetgenJL.num_surface_elements(r) == NetgenJL.num_surface_elements(mesh)
    rm(f; force=true)
    return ok
end

# Sphere hierarchy: geometry-aware snapping must hold on every refined level.
sgeo9 = NetgenJL.sphere_geometry(0.0, 0.0, 0.0, 1.0)
smp9 = NetgenJL.MeshingParameters(); NetgenJL.set_maxh!(smp9, 0.6)
sm0 = NetgenJL.generate_mesh(sgeo9, smp9)
sm0_np = NetgenJL.num_points(sm0)
slevels = build_hierarchy(sm0, 3)
@assert length(slevels) == 4
@assert NetgenJL.num_points(sm0) == sm0_np            # input mesh not mutated
for k in 1:4
    L = slevels[k]
    @assert NetgenJL.geometry_type_name(L) == "csg"
    @assert NetgenJL.has_geometry(L)
    assert_level_extractable(L)
    @assert maximum(abs.(boundary_radii(L, 0.0, 0.0, 0.0) .- 1.0)) < 1e-6  # on sphere
    @assert roundtrip_counts_ok(L)
    if k > 1
        P = slevels[k - 1]
        @assert NetgenJL.num_points(L) > NetgenJL.num_points(P)
        @assert NetgenJL.num_volume_elements(L) > NetgenJL.num_volume_elements(P)
        @assert NetgenJL.num_edges(L) > NetgenJL.num_edges(P)
        @assert NetgenJL.num_faces(L) > NetgenJL.num_faces(P)
        # each refined level records exactly its own single refinement step.
        @assert collect(NetgenJL.level_vertex_counts(L)) ==
                [NetgenJL.num_points(P), NetgenJL.num_points(L)]
    end
end

# Cube hierarchy: parent maps coherent across levels + P1 nodal transfer exact.
cgeo9 = NetgenJL.unit_cube_geometry()
cmp9 = NetgenJL.MeshingParameters(); NetgenJL.set_maxh!(cmp9, 0.5)
cm0 = NetgenJL.generate_mesh(cgeo9, cmp9)
clevels = build_hierarchy(cm0, 3)
ufun9(x, y, z) = 2 - x + 0.25y + 3z
for k in 2:4
    P = clevels[k - 1]; L = clevels[k]
    npP = NetgenJL.num_points(P); npL = NetgenJL.num_points(L)
    par = reshape(NetgenJL.point_parent_vertices_flat(L), 2, npL)
    @assert all(1 .<= par .<= npP)                    # parents index the coarser level
    @assert all(par[1, p] == p && par[2, p] == p for p in 1:npP)   # coarse -> self
    @assert any(par[1, p] != par[2, p] for p in 1:npL)             # has midpoints
    # P1 transfer: parent pairs reproduce a linear function exactly on flat geom.
    cP = reshape(NetgenJL.point_coordinates_flat(P), 3, npP)
    cL = reshape(NetgenJL.point_coordinates_flat(L), 3, npL)
    uP = [ufun9(cP[:, p]...) for p in 1:npP]
    uL = [par[1, p] == par[2, p] ? uP[par[1, p]] : 0.5 * (uP[par[1, p]] + uP[par[2, p]])
          for p in 1:npL]
    uExact = [ufun9(cL[:, p]...) for p in 1:npL]
    @assert maximum(abs.(uL .- uExact)) < 1e-10
end

# --- Sprint 5: optional OCC import (present only if built with USE_OCC) ------
# Skipped cleanly if OCC was not compiled in or no fixture is provided via
# NGJL_OCC_FIXTURE (e.g. an existing tracked file such as tutorials/screw.step).
occ_summary = "not built (USE_OCC=OFF)"
if isdefined(NetgenJL, :load_occ_geometry)
    fixture = get(ENV, "NGJL_OCC_FIXTURE", "")
    if isempty(fixture)
        occ_summary = "built; smoke skipped (set NGJL_OCC_FIXTURE)"
    else
        @assert isfile(fixture)
        ogeo = NetgenJL.load_occ_geometry(fixture)
        ompp = NetgenJL.MeshingParameters()
        omesh = NetgenJL.generate_mesh(ogeo, ompp)
        onp = NetgenJL.num_points(omesh)
        one = NetgenJL.num_volume_elements(omesh)
        onse = NetgenJL.num_surface_elements(omesh)
        @assert onp > 0
        @assert onse > 0

        # Sprint 3 extraction works on the OCC-generated mesh.
        ocoords = reshape(NetgenJL.point_coordinates_flat(omesh), 3, onp)
        @assert size(ocoords) == (3, onp)
        @assert length(NetgenJL.surface_element_types(omesh)) == onse
        @assert length(NetgenJL.surface_connectivity_flat(omesh)) > 0

        # save/load roundtrip preserves counts.
        otmp = tempname() * ".vol"
        NetgenJL.save_mesh(omesh, otmp)
        omesh2 = NetgenJL.load_mesh(otmp)
        @assert NetgenJL.num_points(omesh2) == onp
        @assert NetgenJL.num_surface_elements(omesh2) == onse
        rm(otmp; force=true)

        # Extension dispatch is case-insensitive: load via an uppercased copy
        # (succeeds without throwing on the unsupported-extension path).
        upper = tempname() * uppercase(splitext(fixture)[2])
        cp(fixture, upper)
        ogeo_u = NetgenJL.load_occ_geometry(upper)
        @assert ogeo_u !== nothing
        rm(upper; force=true)

        # Topology extraction works on the OCC-generated mesh.
        @assert NetgenJL.num_edges(omesh) > 0
        @assert NetgenJL.num_faces(omesh) >= NetgenJL.num_surface_elements(omesh)
        @assert NetgenJL.geometry_type_name(omesh) == "occ"

        # Two-level OCC hierarchy: each level is extractable and saveable.
        olevels = build_hierarchy(omesh, 2)
        @assert length(olevels) == 3
        @assert NetgenJL.num_points(omesh) == onp      # input not mutated
        for k in 1:3
            assert_level_extractable(olevels[k])
            @assert roundtrip_counts_ok(olevels[k])
            if k > 1
                @assert NetgenJL.num_points(olevels[k]) > NetgenJL.num_points(olevels[k - 1])
            end
        end

        # Uniform refinement also works in place on an OCC-generated mesh.
        NetgenJL.uniform_refine!(omesh)
        @assert NetgenJL.num_points(omesh) > onp
        @assert NetgenJL.num_surface_elements(omesh) > onse
        @assert NetgenJL.num_edges(omesh) > 0          # topology valid after refine

        occ_summary = "$(basename(fixture)) -> np/ne/nse = $onp/$one/$onse " *
                      "(roundtrip OK; uppercase-ext OK; refine -> $(NetgenJL.num_points(omesh)) pts)"
    end
end

println("Netgen Julia smoke test passed:")
println("  netgen_julia_smoke() = ", NetgenJL.netgen_julia_smoke())
println("  netgen_julia_hello() = \"", NetgenJL.netgen_julia_hello(), "\"")
println("  Point3d(1,2,3)       = (", NetgenJL.x(p), ", ", NetgenJL.y(p), ", ", NetgenJL.z(p), ")")
println("  Vec3d(3,0,4) length  = ", NetgenJL.length(v))
println("  MeshingParameters.maxh = ", NetgenJL.maxh(mp), ", grading = ", NetgenJL.grading(mp),
        ", secondorder = ", NetgenJL.secondorder(mp))
println("  Mesh (empty) np/ne/nse = ", NetgenJL.num_points(m), "/",
        NetgenJL.num_volume_elements(m), "/", NetgenJL.num_surface_elements(m),
        "; save/load roundtrip OK")
println("  Unit tet np/ne/nse   = ", NetgenJL.num_points(tet), "/",
        NetgenJL.num_volume_elements(tet), "/", NetgenJL.num_surface_elements(tet))
println("  Tet volume element   = ", vec(vconn), " (", NetgenJL.element_type_name(vtypes[1]),
        ", 1-based); surface = 4x ", NetgenJL.element_type_name(stypes[1]),
        "; extraction roundtrip OK")
println("  CSG unit cube (maxh 0.5) np/ne/nse = ", gnp, "/", gne, "/", gnse,
        " (all TET/TRIG); generated-mesh roundtrip OK")
println("  Uniform refine: ne ", rne0, " -> ", rne1, " -> ", rne2,
        " (copy_mesh deep-copies; extraction/save+load OK after refine)")
println("  Hierarchy: geometry='", NetgenJL.geometry_type_name(h1), "', levels ",
        collect(NetgenJL.level_vertex_counts(h1)), ", ", length(newnodes),
        " midpoints; P1 nodal transfer max err = ", p1_err)
println("  Curved refine: sphere boundary nodes stay on r=1 after refine ",
        "(max |r-1| = ", sphere_err, ", geometry-aware snapping)")
println("  Topology: unit tet 6 edges / 4 faces (edge orientations verified); ",
        "CSG cube edges ", tne0, " -> ", NetgenJL.num_edges(t0),
        ", faces ", tnf0, " -> ", NetgenJL.num_faces(t0), " under refine")
println("  Hierarchy (4 levels): sphere np ",
        [NetgenJL.num_points(L) for L in slevels],
        " (boundary on r=1, extract+save/load OK); cube parent-map P1 exact per level")
println("  OCC import: ", occ_summary)
