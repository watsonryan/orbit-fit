# @file CMakeLists.txt
# @brief Configure the C++23 library, tools, verification targets, and installed package.
# @author watson
cmake_minimum_required(VERSION 3.28)
project(orbitfit VERSION 0.1.0 LANGUAGES C CXX)
find_package(Threads REQUIRED)
include(cmake/check_toolchain.cmake)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(ORBITFIT_BUILD_TESTS "Build offline tests" ${PROJECT_IS_TOP_LEVEL})
option(ORBITFIT_BUILD_CLI "Build orbit-fit" ${PROJECT_IS_TOP_LEVEL})
option(ORBITFIT_BUILD_BENCHMARKS "Build native derivative and fitting benchmarks" OFF)
option(ORBITFIT_BUILD_QUALIFICATION "Build offline independent-ephemeris qualification tools" OFF)
option(ORBITFIT_BUILD_DOCS "Build strict Doxygen HTML and XML reference documentation" OFF)
option(ORBITFIT_BUILD_FUZZERS "Build Clang libFuzzer parser and serialization campaign" OFF)
set(ORBITFIT_FUZZER_RUNTIME "" CACHE FILEPATH "Optional libFuzzer archive for Clang installations without a bundled runtime")
option(ORBITFIT_FETCH_DEPS "Resolve pinned dependencies with CPM" ON)
option(ORBITFIT_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ORBITFIT_SANITIZERS "Instrument project code with ASan and UBSan" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/dependencies.cmake)
# Upstream headers have their own warning policy.
foreach(dependency ceres eigen glog spdlog yaml-cpp)
  if(TARGET ${dependency})
    set_property(TARGET ${dependency} PROPERTY SYSTEM TRUE)
  endif()
endforeach()

function(orbitfit_options target)
  target_compile_features(${target} PUBLIC cxx_std_23)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /W4 $<$<BOOL:${ORBITFIT_WARNINGS_AS_ERRORS}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
      $<$<BOOL:${ORBITFIT_WARNINGS_AS_ERRORS}>:-Werror>)
    if(ORBITFIT_SANITIZERS)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

# Keep the unmodified upstream kernel out of the project's warning policy.
add_library(orbitfit_vallado OBJECT third_party/vallado/SGP4.cpp)
set_target_properties(orbitfit_vallado PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(ORBITFIT_SANITIZERS AND NOT MSVC)
  target_compile_options(orbitfit_vallado PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_library(orbitfit STATIC src/time.cpp src/sgp4.cpp src/tle.cpp src/fit.cpp src/initialization.cpp
  src/validation.cpp src/chart.cpp src/evaluation.cpp src/estimation.cpp src/derivatives.cpp src/curvature.cpp src/io.cpp src/products.cpp
  $<TARGET_OBJECTS:orbitfit_vallado>)
add_library(orbitfit::orbitfit ALIAS orbitfit)
set_target_properties(orbitfit PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(orbitfit PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(orbitfit SYSTEM PRIVATE third_party/vallado third_party/vallado_ad)
# The adapted kernel includes our private math glue.
target_include_directories(orbitfit PRIVATE src)
target_link_libraries(orbitfit PRIVATE Ceres::ceres Eigen3::Eigen nlohmann_json::nlohmann_json)
target_compile_definitions(orbitfit PRIVATE ORBITFIT_VERSION="${PROJECT_VERSION}"
  ORBITFIT_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
orbitfit_options(orbitfit)

if(ORBITFIT_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT ORBITFIT_SANITIZERS)
    message(FATAL_ERROR "Fuzzing requires Clang and ORBITFIT_SANITIZERS=ON")
  endif()
  target_compile_options(orbitfit PRIVATE -fsanitize=fuzzer-no-link)
  target_compile_options(orbitfit_vallado PRIVATE -fsanitize=fuzzer-no-link)
  add_executable(orbitfit_fuzz fuzz/parsers.cpp)
  target_link_libraries(orbitfit_fuzz PRIVATE orbitfit)
  orbitfit_options(orbitfit_fuzz)
  target_compile_options(orbitfit_fuzz PRIVATE -fsanitize=fuzzer-no-link)
  if(ORBITFIT_FUZZER_RUNTIME)
    if(NOT EXISTS "${ORBITFIT_FUZZER_RUNTIME}")
      message(FATAL_ERROR "ORBITFIT_FUZZER_RUNTIME must name an existing libFuzzer archive")
    endif()
    target_link_libraries(orbitfit_fuzz PRIVATE "${ORBITFIT_FUZZER_RUNTIME}")
  else()
    target_link_options(orbitfit_fuzz PRIVATE -fsanitize=fuzzer)
  endif()
endif()

if(ORBITFIT_BUILD_CLI)
  find_package(Threads REQUIRED)
  add_library(orbitfit_cli_support STATIC apps/cli_support.cpp apps/cli_input.cpp)
  target_include_directories(orbitfit_cli_support PUBLIC apps)
  target_link_libraries(orbitfit_cli_support PUBLIC Threads::Threads orbitfit)
  target_link_libraries(orbitfit_cli_support PRIVATE yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)
  orbitfit_options(orbitfit_cli_support)
  add_executable(orbit-fit apps/main.cpp)
  target_link_libraries(orbit-fit PRIVATE orbitfit_cli_support spdlog::spdlog)
  target_compile_definitions(orbit-fit PRIVATE ORBITFIT_VERSION="${PROJECT_VERSION}")
  orbitfit_options(orbit-fit)
  install(TARGETS orbit-fit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES third_party/spdlog-LICENSE third_party/yaml-cpp-LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/orbitfit)
endif()

if(ORBITFIT_BUILD_TESTS)
  enable_testing()
  add_test(NAME authored_headers COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/check_headers.cmake)
  add_test(NAME fixture_hashes COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/tests/data/check.cmake)
  include(GoogleTest)
  add_executable(orbitfit_tests tests/test_time_formats.cpp tests/test_propagation.cpp tests/test_fit.cpp
    tests/test_derivatives.cpp tests/test_curvature.cpp tests/test_robustness.cpp tests/test_independent.cpp tests/test_products.cpp)
  target_link_libraries(orbitfit_tests PRIVATE orbitfit GTest::gtest_main Eigen3::Eigen nlohmann_json::nlohmann_json)
  target_include_directories(orbitfit_tests PRIVATE src)
  target_compile_definitions(orbitfit_tests PRIVATE ORBITFIT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  orbitfit_options(orbitfit_tests)
  gtest_discover_tests(orbitfit_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 120)
  add_executable(orbitfit_example examples/fit.cpp)
  target_link_libraries(orbitfit_example PRIVATE orbitfit)
  orbitfit_options(orbitfit_example)
  add_test(NAME documented_example COMMAND orbitfit_example)
  foreach(example forecast cancellation)
    add_executable(orbitfit_${example}_example examples/${example}.cpp)
    target_link_libraries(orbitfit_${example}_example PRIVATE orbitfit)
    orbitfit_options(orbitfit_${example}_example)
  endforeach()
  add_test(NAME documented_forecast COMMAND orbitfit_forecast_example
    ${PROJECT_SOURCE_DIR}/tests/data/independent/iss-teme.csv)
  add_test(NAME documented_cancellation COMMAND orbitfit_cancellation_example)
  if(ORBITFIT_BUILD_CLI)
    add_executable(orbitfit_cli_tests tests/test_cli_support.cpp tests/test_cli_input.cpp)
    target_link_libraries(orbitfit_cli_tests PRIVATE orbitfit_cli_support GTest::gtest_main
      nlohmann_json::nlohmann_json)
    orbitfit_options(orbitfit_cli_tests)
    gtest_discover_tests(orbitfit_cli_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 30)
    if(UNIX)
      add_executable(orbitfit_cli_cancel_test tests/cli_cancel.cpp)
      target_link_libraries(orbitfit_cli_cancel_test PRIVATE orbitfit_cli_support nlohmann_json::nlohmann_json)
      orbitfit_options(orbitfit_cli_cancel_test)
      add_test(NAME cli_process_cancellation COMMAND orbitfit_cli_cancel_test $<TARGET_FILE:orbit-fit>)
      set_tests_properties(cli_process_cancellation PROPERTIES TIMEOUT 30)
    endif()
    add_test(NAME cli_help COMMAND orbit-fit --help)
    add_test(NAME cli_example COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:orbit-fit> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/example-output -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_example.cmake)
    add_test(NAME cli_logging COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:orbit-fit> -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/logging-output
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_logging.cmake)
    add_test(NAME cli_yaml COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:orbit-fit> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/yaml-output -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_yaml.cmake)
  endif()
endif()

if(ORBITFIT_BUILD_BENCHMARKS)
  add_executable(orbitfit_benchmark benchmarks/derivatives.cpp)
  target_link_libraries(orbitfit_benchmark PRIVATE orbitfit Eigen3::Eigen)
  target_include_directories(orbitfit_benchmark PRIVATE src tests)
  target_compile_definitions(orbitfit_benchmark PRIVATE ORBITFIT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  orbitfit_options(orbitfit_benchmark)
  add_executable(orbitfit_curvature_benchmark benchmarks/curvature.cpp)
  target_link_libraries(orbitfit_curvature_benchmark PRIVATE orbitfit Eigen3::Eigen)
  target_include_directories(orbitfit_curvature_benchmark PRIVATE src tests)
  target_compile_definitions(orbitfit_curvature_benchmark PRIVATE ORBITFIT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  orbitfit_options(orbitfit_curvature_benchmark)
endif()

if(ORBITFIT_BUILD_QUALIFICATION)
  add_subdirectory(qualification)
endif()

if(ORBITFIT_BUILD_DOCS)
  find_package(Doxygen 1.9.8 REQUIRED)
  configure_file(docs/Doxyfile.in Doxyfile @ONLY)
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/docs/benchmarks
      ${CMAKE_CURRENT_BINARY_DIR}/docs/html/benchmarks
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/examples
      ${CMAKE_CURRENT_BINARY_DIR}/docs/examples
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/tests/consumer
      ${CMAKE_CURRENT_BINARY_DIR}/docs/tests/consumer
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/tests/cpm_consumer
      ${CMAKE_CURRENT_BINARY_DIR}/docs/tests/cpm_consumer
    COMMAND ${CMAKE_COMMAND} -DHTML_ROOT=${CMAKE_CURRENT_BINARY_DIR}/docs/html
      -P ${PROJECT_SOURCE_DIR}/docs/check_links.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Build Doxygen documentation; warnings fail the target" VERBATIM)
endif()

install(TARGETS orbitfit EXPORT orbitfitTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT orbitfitTargets NAMESPACE orbitfit:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orbitfit)
configure_package_config_file(cmake/orbitfitConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/orbitfitConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orbitfit)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/orbitfitConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/orbitfitConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/orbitfitConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orbitfit)
install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/orbitfit)
install(FILES third_party/vallado/NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/orbitfit/vallado)
install(FILES third_party/ceres-LICENSE third_party/glog-LICENSE third_party/eigen-COPYING.MPL2
  DESTINATION ${CMAKE_INSTALL_DATADIR}/orbitfit)

if(PROJECT_IS_TOP_LEVEL)
  add_custom_target(package-check
    COMMAND ${CMAKE_COMMAND} "-DBINARY_DIR=${PROJECT_BINARY_DIR}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}" "-DCOMPILER=${CMAKE_CXX_COMPILER}"
      "-DCPM_CACHE=${CPM_SOURCE_CACHE}" "-DCONFIG=$<CONFIG>"
      -P ${PROJECT_SOURCE_DIR}/tests/package.cmake
    DEPENDS orbitfit $<$<BOOL:${ORBITFIT_BUILD_CLI}>:orbit-fit>
    COMMENT "Install, relocate, and verify an isolated package consumer" VERBATIM)
endif()
