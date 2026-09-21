find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GUIDE_HEADER ${REPO_ROOT}/src/util/UserGuide.generated.h)
add_custom_command(OUTPUT ${GUIDE_HEADER}
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/generate_userguide_epub.py --bundled
  DEPENDS ${REPO_ROOT}/scripts/generate_userguide_epub.py ${REPO_ROOT}/scripts/gen_boot_logo.py
          ${REPO_ROOT}/docs/user-guide/en.xhtml ${REPO_ROOT}/docs/user-guide/zh-CN.xhtml
)
add_executable(UserGuideTest UserGuideTest.cpp ${REPO_ROOT}/src/util/UserGuide.cpp ${GUIDE_HEADER})
target_include_directories(UserGuideTest BEFORE PRIVATE stubs ${REPO_ROOT}/src/util)
target_link_libraries(UserGuideTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(UserGuideTest)
add_test(NAME UserGuideEpub
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_userguide_epub.py)
