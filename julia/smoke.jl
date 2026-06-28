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
