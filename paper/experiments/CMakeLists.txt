cmake_minimum_required(VERSION 3.16)
project(strata_experiments LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Build strata_core directly from source (no ROS/ament required).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../strata_core strata_core_build)

foreach(exp e0_calibration e1_static_quality e2_periodicity e3_sensitivity e4_throughput e5_trajectory)
  add_executable(${exp} src/${exp}.cpp)
  target_link_libraries(${exp} PRIVATE strata_core)
  target_include_directories(${exp} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endforeach()
