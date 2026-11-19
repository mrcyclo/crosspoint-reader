add_executable(AutoSyncCoreTest
  AutoSyncCoreTest.cpp
  ${REPO_ROOT}/lib/AutoSync/AutoSyncCore.cpp
)

target_include_directories(AutoSyncCoreTest PRIVATE
  ${REPO_ROOT}/lib/AutoSync
)

target_link_libraries(AutoSyncCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AutoSyncCoreTest)
