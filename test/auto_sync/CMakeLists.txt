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

add_executable(OpdsSyncFeedTest
  OpdsSyncFeedTest.cpp
  ${REPO_ROOT}/lib/AutoSync/OpdsSyncFeed.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(OpdsSyncFeedTest PRIVATE
  ${REPO_ROOT}/lib/AutoSync
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(OpdsSyncFeedTest PRIVATE
  XML_CONTEXT_BYTES=1024
  XML_GE=0
)

target_link_libraries(OpdsSyncFeedTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsSyncFeedTest)
