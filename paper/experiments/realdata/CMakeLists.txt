# Phase 1 real-data hit-rate probe. Uses strata_core from source (no ROS). Not part of run_all.sh:
# it needs data under ~/datasets/strata that is not in the repository (see reproduce_p1.sh).
add_executable(hitrate_probe hitrate_probe.cpp)
target_link_libraries(hitrate_probe PRIVATE strata_core)
target_include_directories(hitrate_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Phase 1b tools (PREREG_P1B.md): gates, Spires readers, labels, the lean voxel probe.
find_package(ZLIB REQUIRED)
add_executable(p1b_probe p1b_probe.cpp)
target_link_libraries(p1b_probe PRIVATE strata_core ZLIB::ZLIB)
target_include_directories(p1b_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Phase 3 harness (PREREG_P3.md): arms, readers for the test datasets, KTH exporter, labelled-cell evaluation.
add_executable(p3_probe p3_probe.cpp)
target_link_libraries(p3_probe PRIVATE strata_core ZLIB::ZLIB)
target_include_directories(p3_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(GTest QUIET)
if(GTest_FOUND)
  add_executable(test_p1b test_p1b.cpp)
  target_link_libraries(test_p1b PRIVATE strata_core ZLIB::ZLIB GTest::gtest GTest::gtest_main)
  target_include_directories(test_p1b PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(test_p1b PRIVATE P1B_PROBE_EXE="$<TARGET_FILE:p1b_probe>")
  add_dependencies(test_p1b p1b_probe)
  add_test(NAME test_p1b COMMAND test_p1b)
  add_executable(test_p3 test_p3.cpp)
  target_link_libraries(test_p3 PRIVATE strata_core ZLIB::ZLIB GTest::gtest GTest::gtest_main)
  target_include_directories(test_p3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(test_p3 PRIVATE P3_PROBE_EXE="$<TARGET_FILE:p3_probe>")
  add_dependencies(test_p3 p3_probe)
  add_test(NAME test_p3 COMMAND test_p3)
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
add_test(NAME p3_guard
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_p3_guard.sh)
add_test(NAME reproduce_tree_check
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_reproduce_tree_check.sh)
add_test(NAME analysis_unit_tests
         COMMAND python3 -I -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_*.py")
