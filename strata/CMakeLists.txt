cmake_minimum_required(VERSION 3.16)
project(strata VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(ament_cmake REQUIRED)
find_package(strata_core REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_eigen REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(PCL REQUIRED COMPONENTS common io)
find_package(Eigen3 REQUIRED)
link_directories(${PCL_LIBRARY_DIRS})
add_definitions(${PCL_DEFINITIONS})

set(deps
  strata_core rclcpp rclcpp_components tf2 tf2_ros tf2_eigen tf2_geometry_msgs
  sensor_msgs nav_msgs geometry_msgs std_msgs std_srvs pcl_conversions)

add_library(strata_node SHARED
  src/mapping_node.cpp
  src/scan_adapter.cpp
  src/cloud_adapter.cpp)
target_include_directories(strata_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${PCL_INCLUDE_DIRS})
target_link_libraries(strata_node ${PCL_LIBRARIES} Eigen3::Eigen)
ament_target_dependencies(strata_node ${deps})

add_executable(strata_node_main src/main.cpp)
target_link_libraries(strata_node_main strata_node)
ament_target_dependencies(strata_node_main rclcpp)

install(TARGETS strata_node
  ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(TARGETS strata_node_main DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch params rviz DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_grid_math test/test_grid_math.cpp)
  target_link_libraries(test_grid_math strata_core::strata_core)
  ament_add_gtest(test_scan_adapter test/test_scan_adapter.cpp)
  target_link_libraries(test_scan_adapter strata_node)
  ament_target_dependencies(test_scan_adapter ${deps})
endif()

ament_package()
