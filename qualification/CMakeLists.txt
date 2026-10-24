# @file CMakeLists.txt
# @brief Build optional independent-ephemeris qualification tools.
# @author watson
if(ORBITFIT_FETCH_DEPS)
  include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)
  CPMAddPackage(NAME erfa VERSION 2.0.1 DOWNLOAD_ONLY YES
    URL https://codeload.github.com/liberfa/erfa/tar.gz/refs/tags/v2.0.1
    URL_HASH SHA256=d5469fbd0b212b3c7270c1da15c9bd82f37da9218fc89627f98283d27b416cbf)
  # This subset is used only by the offline fixture converter. It is not linked
  # into orbitfit or installed with the library. Preserve upstream source notices.
  set(erfa_units pnm80 pmat76 prec76 numat nut80 nutm80 obl80 ir rx ry rz rxr cr cp anpm)
  list(TRANSFORM erfa_units PREPEND "${erfa_SOURCE_DIR}/src/")
  list(TRANSFORM erfa_units APPEND ".c")
  add_library(orbitfit_qualification_erfa STATIC ${erfa_units})
  target_include_directories(orbitfit_qualification_erfa SYSTEM PUBLIC ${erfa_SOURCE_DIR}/src)
else()
  find_path(ORBITFIT_ERFA_INCLUDE_DIR erfa.h REQUIRED)
  find_library(ORBITFIT_ERFA_LIBRARY NAMES erfa REQUIRED)
  add_library(orbitfit_qualification_erfa INTERFACE)
  target_include_directories(orbitfit_qualification_erfa SYSTEM INTERFACE ${ORBITFIT_ERFA_INCLUDE_DIR})
  target_link_libraries(orbitfit_qualification_erfa INTERFACE ${ORBITFIT_ERFA_LIBRARY})
endif()
add_executable(orbitfit_qualification iss.cpp)
target_link_libraries(orbitfit_qualification PRIVATE orbitfit orbitfit_qualification_erfa)
orbitfit_options(orbitfit_qualification)
if(ORBITFIT_BUILD_TESTS)
  add_test(NAME qualification_frames COMMAND orbitfit_qualification check-frames)
endif()
