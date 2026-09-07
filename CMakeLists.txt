cmake_minimum_required(VERSION 3.16)
project(mltmolten C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Mlt7 REQUIRED)
find_package(PkgConfig REQUIRED)
# Only used for its custom variables (moduledir, mltdatadir) - the module
# itself links against the Mlt7::mlt CMake target above, not pkg-config's
# compile/link flags. pkg_check_modules doesn't import non-standard
# variables on its own, hence pkg_get_variable below.
pkg_check_modules(MLT_PC REQUIRED mlt-framework-7)
pkg_get_variable(MLT_MODULE_DIR mlt-framework-7 moduledir)
pkg_get_variable(MLT_DATA_DIR mlt-framework-7 mltdatadir)
if(NOT MLT_MODULE_DIR OR NOT MLT_DATA_DIR)
    message(FATAL_ERROR "mlt-framework-7.pc did not report moduledir/mltdatadir - unexpected MLT install layout")
endif()

# molten-ffi is not vendored here and not yet fetched automatically - see
# README.md. Point these at a checkout of the `molten` repo's molten-ffi
# crate: the header lives in its source tree, the built library under
# whichever cargo profile directory was built.
#
#   cd /path/to/molten && cargo build -p molten-ffi --release
#   cmake -B build \
#     -DMOLTEN_FFI_INCLUDE_DIR=/path/to/molten/molten-ffi/include \
#     -DMOLTEN_FFI_LIB_DIR=/path/to/molten/target/release
#
set(MOLTEN_FFI_INCLUDE_DIR "" CACHE PATH "Directory containing molten.h")
set(MOLTEN_FFI_LIB_DIR "" CACHE PATH "Directory containing libmolten_ffi.a")
if(NOT MOLTEN_FFI_INCLUDE_DIR OR NOT MOLTEN_FFI_LIB_DIR)
    message(FATAL_ERROR "MOLTEN_FFI_INCLUDE_DIR and MOLTEN_FFI_LIB_DIR are not set - see README.md")
endif()
if(NOT EXISTS "${MOLTEN_FFI_INCLUDE_DIR}/molten.h")
    message(FATAL_ERROR "${MOLTEN_FFI_INCLUDE_DIR}/molten.h not found - is MOLTEN_FFI_INCLUDE_DIR correct?")
endif()
if(NOT EXISTS "${MOLTEN_FFI_LIB_DIR}/libmolten_ffi.a")
    message(FATAL_ERROR "${MOLTEN_FFI_LIB_DIR}/libmolten_ffi.a not found - is MOLTEN_FFI_LIB_DIR correct, and was it built?")
endif()

add_library(mltmolten MODULE
    src/factory.c
    src/producer_molten.c
)

target_include_directories(mltmolten PRIVATE "${MOLTEN_FFI_INCLUDE_DIR}")
target_link_libraries(mltmolten PRIVATE
    Mlt7::mlt
    "${MOLTEN_FFI_LIB_DIR}/libmolten_ffi.a"
)

# molten-ffi is a Rust staticlib; its runtime needs these even though this
# module's own code doesn't call into them directly.
if(UNIX AND NOT APPLE)
    target_link_libraries(mltmolten PRIVATE pthread dl m)
endif()

set_target_properties(mltmolten PROPERTIES PREFIX "")

install(TARGETS mltmolten LIBRARY DESTINATION "${MLT_MODULE_DIR}")
install(FILES src/producer_molten.yml DESTINATION "${MLT_DATA_DIR}/molten")
