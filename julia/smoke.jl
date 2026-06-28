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

@assert NetgenJL.netgen_julia_smoke() == 42
@assert NetgenJL.netgen_julia_hello() == "netgen-julia-ok"

println("Netgen Julia smoke test passed: ",
        "netgen_julia_smoke()=", NetgenJL.netgen_julia_smoke(),
        ", netgen_julia_hello()=\"", NetgenJL.netgen_julia_hello(), "\"")
