cmake_minimum_required(VERSION 3.16)
project(strata_core VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Eigen3 REQUIRED)

# Library — geometry-free LayeredMap engine + PeriodicityModel + 2D/3D backends.
add_library(strata_core
  src/types.cpp
  src/periodicity.cpp
  src/layered_map.cpp
  src/grid2d_backend.cpp
  src/voxel3d_backend.cpp
)
target_include_directories(strata_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(strata_core PUBLIC Eigen3::Eigen)
target_compile_features(strata_core PUBLIC cxx_std_17)

option(STRATA_CORE_BUILD_TESTS "Build strata_core tests" OFF)

# Dual-mode test wiring: ament when available, plain CTest+system gtest otherwise.
if(DEFINED ENV{AMENT_PREFIX_PATH} AND COMMAND ament_package)
  set(_use_ament ON)
endif()

if(BUILD_TESTING OR STRATA_CORE_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  set(STRATA_CORE_TESTS
    test/test_smoke.cpp
    test/test_types.cpp
    test/test_periodicity.cpp
    test/test_layered_map.cpp
    test/test_grid2d_backend.cpp
    test/test_voxel3d_backend.cpp
    test/test_integration.cpp)
  add_executable(strata_core_tests ${STRATA_CORE_TESTS})
  target_link_libraries(strata_core_tests strata_core GTest::gtest GTest::gtest_main)
  add_test(NAME strata_core_tests COMMAND strata_core_tests)
endif()

# ament install/export only when built inside a ROS 2 workspace.
find_package(ament_cmake QUIET)
if(ament_cmake_FOUND)
  install(DIRECTORY include/ DESTINATION include)
  install(TARGETS strata_core EXPORT export_strata_core
    ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin INCLUDES DESTINATION include)
  ament_export_targets(export_strata_core HAS_LIBRARY_TARGET)
  ament_export_dependencies(Eigen3)
  ament_package()
endif()
