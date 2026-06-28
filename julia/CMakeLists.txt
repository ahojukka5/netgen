# Experimental, opt-in Julia bindings for Netgen.
#
# Enabled only with -DUSE_JULIA=ON (see the top-level CMakeLists.txt). This is a
# minimal CxxWrap/JlCxx skeleton that mirrors the optional Python binding target
# (python/ and ng/netgenpy.cpp). For now it builds a single shared library
# (libngjl) exposing one trivial smoke function; it deliberately does not link
# nglib or wrap any Netgen classes yet.
#
# To configure, point CMake at the JlCxx package shipped with the Julia CxxWrap
# artifact, e.g.:
#
#   julia -e 'using CxxWrap; print(CxxWrap.prefix_path())'
#   cmake -DUSE_JULIA=ON -DJlCxx_DIR=<that_path>/lib/cmake/JlCxx ...

find_package(JlCxx CONFIG REQUIRED)

add_library(ngjl SHARED netgenjl.cpp)
target_compile_features(ngjl PRIVATE cxx_std_17)
target_link_libraries(ngjl PRIVATE JlCxx::cxxwrap_julia)

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
