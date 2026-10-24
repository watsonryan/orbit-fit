# @file CMakeLists.txt
# @brief Exercise the documented CPM source dependency without installing orbit-fit first.
# @author watson
cmake_minimum_required(VERSION 3.28)
project(orbitfit_cpm_consumer LANGUAGES C CXX)
if(NOT ORBITFIT_SOURCE_DIR)
  message(FATAL_ERROR "Set ORBITFIT_SOURCE_DIR to the local checkout")
endif()
include("${ORBITFIT_SOURCE_DIR}/cmake/CPM.cmake")
CPMAddPackage(NAME orbitfit SOURCE_DIR "${ORBITFIT_SOURCE_DIR}"
  OPTIONS "ORBITFIT_BUILD_TESTS OFF" "ORBITFIT_BUILD_CLI OFF"
    "ORBITFIT_BUILD_DOCS OFF" "ORBITFIT_BUILD_QUALIFICATION OFF"
    "ORBITFIT_BUILD_BENCHMARKS OFF" "ORBITFIT_WARNINGS_AS_ERRORS ON")
add_executable(cpm_consumer "${CMAKE_CURRENT_LIST_DIR}/../consumer/main.cpp")
target_link_libraries(cpm_consumer PRIVATE orbitfit::orbitfit)
enable_testing()
add_test(NAME cpm_consumer COMMAND cpm_consumer)
