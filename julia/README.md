# Experimental Julia bindings (`libngjl`)

This directory contains an **experimental, opt-in** Julia binding for Netgen.
It is disabled by default and is not part of a normal Netgen build.

## What it is

* Built with [CxxWrap.jl](https://github.com/JuliaInterop/CxxWrap.jl) / JlCxx.
* Mirrors the *idea* of the optional Python binding (`python/`, `ng/netgenpy.cpp`),
  but does **not** use pybind11 and does **not** depend on `libngpy`.
* Produces a single shared library, `libngjl`, that links `nglib`.
* Wraps a small, deliberately incomplete slice of Netgen so that external Julia
  FEM / multiphysics codes can drive meshing:
  * meshing value types: `Point3d`, `Vec3d`, `MeshingParameters`;
  * a `std::shared_ptr<Mesh>` handle with load/save and read-only bulk
    extraction of coordinates and element connectivity/types;
  * a minimal CSG mesh-generation path (axis-aligned box, sphere → `GenerateMesh`)
    and optional OCC file import;
  * geometry-aware uniform refinement with refinement-hierarchy parent maps;
  * generic topology / incidence: global edges and faces, element→edge/face
    maps, edge orientation signs, and the surface→face map.

It is **not** a full Netgen Julia API.

## Building

`USE_JULIA` is `OFF` by default. A default Netgen build never searches for Julia
or JlCxx. To enable the binding:

```bash
JLCXX=$(julia -e 'using CxxWrap; print(CxxWrap.prefix_path())')/lib/cmake/JlCxx
cmake -DUSE_JULIA=ON -DUSE_CSG=ON -DJlCxx_DIR="$JLCXX" ...
cmake --build <build> --target ngjl
julia julia/smoke.jl <build>/julia/libngjl.dylib
```

`USE_JULIA=ON` currently requires `USE_CSG=ON` (configure fails with a clear
message otherwise), because the binding includes CSG mesh generation.

### Optional OCC (OpenCASCADE) support

OCC import is conditional on `USE_OCC`. The non-OCC binding always builds; OCC
file import (`load_occ_geometry`) appears only when `USE_OCC=ON`:

```bash
cmake -DUSE_JULIA=ON -DUSE_CSG=ON -DUSE_OCC=ON \
      -DJlCxx_DIR="$JLCXX" -DOpenCASCADE_DIR=<occ-cmake-dir> ...
cmake --build <build> --target ngjl
# OCC smoke is opt-in: point it at a small CAD file (the repo ships a couple):
NGJL_OCC_FIXTURE=tutorials/screw.step julia julia/smoke.jl <build>/julia/libngjl.dylib
```

Unlike CSG, the OCC binding needs **no new exported symbols**: the `OCCGeometry`
class and the `LoadOCC_STEP/BREP/IGES` loaders are already `DLL_HEADER`-exported
by `nglib`. The Julia OCC glue is built only when `USE_OCC=ON`
(`julia_occ.cpp`, guarded by the `NGJL_HAS_OCC` compile definition) and links
the OpenCASCADE libraries via Netgen's `occ_libs` target.

## Symbol visibility / export notes

Two visibility details are worth understanding for review:

1. **The `ngjl` target uses default symbol visibility.** Netgen builds with
   hidden visibility by default (`CMAKE_CXX_VISIBILITY_PRESET hidden`). CxxWrap
   relies on *default* visibility so that its `type_info`-keyed type-factory
   registry is deduplicated across the `libcxxwrap_julia` / `libngjl` boundary;
   with hidden visibility, type registration fails at load time
   ("No appropriate factory for type ..."). `julia/CMakeLists.txt` restores
   default visibility for this target only.

2. **CSG required a few additional exported symbols.** `libngjl` is a *separate*
   shared library, so it can only call symbols that `nglib` exports
   (`DLL_HEADER`). Most of the Mesh / MeshingParameters API is already exported,
   but constructing a CSG geometry needed a few small `DLL_HEADER` additions in
   the CSG headers: the `OrthoBrick(Point,Point)`, `Sphere(Point,double)` and
   `Solid(Primitive*)` constructors and the `Solid` block-allocator static used
   by `Solid`'s inline `operator new`. These are additive visibility annotations
   only; the default build is behaviourally unchanged. (OCC needed no new
   exports — `OCCGeometry` and the `LoadOCC_*` loaders are already exported.)

   The Python binding avoids such exports because `python_*.cpp` is compiled
   *into* `nglib`, so it links the internal (hidden) symbols directly.

### Long-term design choices

For an upstream discussion, there are two reasonable directions:

* keep a minimal set of `DLL_HEADER` exports so external C++ language bindings
  (Julia, or others) can construct the relevant types; or
* compile the Julia glue into the same library boundary as the implementation it
  wraps (analogous to how the Python binding is compiled into `nglib`), which
  avoids new exports at the cost of coupling the Julia sources into that build.

This branch currently takes the first approach for CSG, kept as small and
explicit as possible.
