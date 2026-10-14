# Phase 1 real-data hit-rate probe. Uses strata_core from source (no ROS). Not part of run_all.sh:
# it needs data under ~/datasets/strata that is not in the repository (see reproduce_p1.sh).
add_executable(hitrate_probe hitrate_probe.cpp)
target_link_libraries(hitrate_probe PRIVATE strata_core)
target_include_directories(hitrate_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(GTest QUIET)
if(GTest_FOUND)
  add_executable(test_realdata test_realdata.cpp)
  target_link_libraries(test_realdata PRIVATE strata_core GTest::gtest GTest::gtest_main)
  target_include_directories(test_realdata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(test_realdata PRIVATE HITRATE_PROBE_EXE="$<TARGET_FILE:hitrate_probe>")
  add_dependencies(test_realdata hitrate_probe)
  add_test(NAME test_realdata COMMAND test_realdata)
endif()
add_test(NAME no_data_staged
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_no_data_staged.sh)
add_test(NAME no_data_staged_selftest
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_no_data_staged.sh --selftest)
add_test(NAME check_realdata_script
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_check_realdata.sh)
add_test(NAME analysis_unit_tests
         COMMAND python3 -I -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_*.py")
