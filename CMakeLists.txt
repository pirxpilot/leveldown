cmake_minimum_required(VERSION 2.8)

# Name of the project (will be the name of the plugin)
project(leveldown)

# crc32c

set(CRC32C_BUILD_TESTS OFF CACHE BOOL "Build CRC32C's unit tests")
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "Build CRC32C's benchmarks")
set(CRC32C_USE_GLOG OFF CACHE BOOL "Build CRC32C's tests with Google Logging")
set(CRC32C_INSTALL OFF CACHE BOOL "Install CRC32C's header and library")

set(HAVE_CRC32C ON CACHE BOOL "CRC32C is build here")

add_subdirectory(lib/crc32c)

set_target_properties(crc32c PROPERTIES COMPILE_FLAGS "-fPIC -fno-builtin-memcmp")
set_target_properties(crc32c_sse42 PROPERTIES COMPILE_FLAGS "-fPIC -fno-builtin-memcmp")
set_target_properties(crc32c_arm64 PROPERTIES COMPILE_FLAGS "-fPIC -fno-builtin-memcmp")

# snappy

set(SNAPPY_BUILD_TESTS CACHE BOOL "Don not Build Snappy's own tests.")

set(HAVE_SNAPPY ON CACHE BOOL "snappy is build also")

add_subdirectory(lib/snappy)

set_target_properties(snappy PROPERTIES COMPILE_FLAGS "-fPIC -fno-builtin-memcmp")

# leveldb

set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "do not build benchmarks")
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "do not build benchmarks")
set(LEVELDB_INSTALL OFF CACHE BOOL "do not install")

add_subdirectory(lib/leveldb)

set_target_properties(leveldb PROPERTIES COMPILE_FLAGS "-fPIC -fno-builtin-memcmp")

include_directories(
  SYSTEM
  "${PROJECT_SOURCE_DIR}/lib/crc32c/include"
  "${PROJECT_SOURCE_DIR}/lib/snappy"
  "${PROJECT_SOURCE_DIR}/lib/leveldb/include"
)

# Build a shared library named after the project from the files in `src/`
file(GLOB SOURCE_FILES "src/*.cc" "src/*.h")
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

# Gives our library file a .node extension without any "lib" prefix
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

# Essential include files to build a node addon,
# You should add this line in every CMake.js based project
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_JS_INC})

# Essential library files to link to a node addon
# You should add this line in every CMake.js based project
target_link_libraries(
  ${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  crc32c
  snappy
  leveldb
)
