cmake_minimum_required(VERSION 3.28)
project(orbitfit VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(ORBITFIT_BUILD_TESTS "Build offline tests" ${PROJECT_IS_TOP_LEVEL})
option(ORBITFIT_BUILD_CLI "Build orbit-fit" ${PROJECT_IS_TOP_LEVEL})
option(ORBITFIT_FETCH_DEPS "Resolve pinned dependencies with CPM" ON)
option(ORBITFIT_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ORBITFIT_SANITIZERS "Instrument project code with ASan and UBSan" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/dependencies.cmake)
# Upstream headers have their own warning policy.
set_property(TARGET gtsam PROPERTY SYSTEM TRUE)

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

add_library(orbitfit STATIC src/time.cpp src/sgp4.cpp src/tle.cpp src/fit.cpp src/io.cpp
  $<TARGET_OBJECTS:orbitfit_vallado>)
add_library(orbitfit::orbitfit ALIAS orbitfit)
set_target_properties(orbitfit PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(orbitfit PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(orbitfit SYSTEM PRIVATE third_party/vallado)
target_link_libraries(orbitfit PRIVATE gtsam nlohmann_json::nlohmann_json)
target_compile_definitions(orbitfit PRIVATE ORBITFIT_VERSION="${PROJECT_VERSION}"
  ORBITFIT_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
orbitfit_options(orbitfit)

if(ORBITFIT_BUILD_CLI)
  add_executable(orbit-fit apps/main.cpp)
  target_link_libraries(orbit-fit PRIVATE orbitfit)
  target_compile_definitions(orbit-fit PRIVATE ORBITFIT_VERSION="${PROJECT_VERSION}")
  orbitfit_options(orbit-fit)
  install(TARGETS orbit-fit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(ORBITFIT_BUILD_TESTS)
  enable_testing()
  include(GoogleTest)
  add_executable(orbitfit_tests tests/test_time_formats.cpp tests/test_propagation.cpp tests/test_fit.cpp)
  target_link_libraries(orbitfit_tests PRIVATE orbitfit GTest::gtest_main gtsam)
  target_include_directories(orbitfit_tests PRIVATE src)
  target_compile_definitions(orbitfit_tests PRIVATE ORBITFIT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  orbitfit_options(orbitfit_tests)
  gtest_discover_tests(orbitfit_tests DISCOVERY_MODE PRE_TEST)
  if(ORBITFIT_BUILD_CLI)
    add_test(NAME cli_help COMMAND orbit-fit --help)
    add_test(NAME cli_example COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:orbit-fit> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/example-output -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_example.cmake)
  endif()
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
