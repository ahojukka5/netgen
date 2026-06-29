# Experimental, opt-in Julia bindings for Netgen.
#
# Enabled only with -DUSE_JULIA=ON (see the top-level CMakeLists.txt). Built with
# CxxWrap/JlCxx, this mirrors the idea of the optional Python binding (python/ and
# ng/netgenpy.cpp) but does not use pybind11 or libngpy. It builds a single shared
# library (libngjl) that links nglib and currently wraps a small, deliberately
# incomplete slice of Netgen: basic meshing value types (Point3d, Vec3d,
# MeshingParameters), a shared_ptr<Mesh> handle with load/save and read-only
# bulk extraction, and a minimal CSG mesh-generation path. It is not a full
# Netgen Julia API. See README.md for the visibility/export rationale.
#
# To configure, point CMake at the JlCxx package shipped with the Julia CxxWrap
# artifact, e.g.:
#
#   julia -e 'using CxxWrap; print(CxxWrap.prefix_path())'
#   cmake -DUSE_JULIA=ON -DJlCxx_DIR=<that_path>/lib/cmake/JlCxx ...

find_package(JlCxx CONFIG REQUIRED)

set(NGJL_SOURCES
  netgenjl.cpp
  julia_meshing.cpp
  julia_mesh.cpp
  julia_csg.cpp
)

# OCC support is conditional: the non-OCC binding (meshing + Mesh + CSG) always
# builds; OCC import is added only when USE_OCC=ON.
if(USE_OCC)
  list(APPEND NGJL_SOURCES julia_occ.cpp)
endif(USE_OCC)

add_library(ngjl SHARED ${NGJL_SOURCES})
target_compile_features(ngjl PRIVATE cxx_std_17)
# JlCxx for the binding glue; nglib provides the out-of-line Netgen symbols
# (e.g. MeshingParameters' constructor). Point3d/Vec3d are header-only.
target_link_libraries(ngjl PRIVATE JlCxx::cxxwrap_julia nglib)

if(USE_OCC)
  # OCCGeometry and the LoadOCC_* loaders are already exported by nglib; we just
  # need the OpenCASCADE headers/libs and the NGJL_HAS_OCC switch. (-DOCCGEOMETRY
  # is already added globally by the top-level CMakeLists when USE_OCC=ON.)
  target_compile_definitions(ngjl PRIVATE NGJL_HAS_OCC)
  target_link_libraries(ngjl PRIVATE occ_libs)
endif(USE_OCC)

# Netgen builds with hidden symbol visibility by default (CMAKE_CXX_VISIBILITY_PRESET
# in the top-level CMakeLists.txt). CxxWrap/JlCxx relies on *default* visibility so
# that its type_info-keyed type factory registry is deduplicated across the
# libcxxwrap_julia / libngjl boundary. With hidden visibility the registration fails
# at load time with "No appropriate factory for type ...". Restore default visibility
# for this target only.
set_target_properties(ngjl PROPERTIES
  CXX_VISIBILITY_PRESET default
  VISIBILITY_INLINES_HIDDEN OFF)

# Make the JlCxx runtime discoverable when loading libngjl straight from the
# build tree (the smoke test in this directory does exactly that).
get_target_property(_jlcxx_lib JlCxx::cxxwrap_julia IMPORTED_LOCATION_RELEASE)
if(_jlcxx_lib)
  get_filename_component(_jlcxx_dir "${_jlcxx_lib}" DIRECTORY)
  set_target_properties(ngjl PROPERTIES BUILD_RPATH "${_jlcxx_dir}")
endif()

message(STATUS "Building experimental Julia binding target 'ngjl' (USE_JULIA=ON)")
