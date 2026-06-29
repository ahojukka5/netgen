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
