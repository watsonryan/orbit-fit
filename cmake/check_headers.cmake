# @file check_headers.cmake
# @brief Enforce purpose and watson author headers on all project-authored text files.
# @author watson
cmake_minimum_required(VERSION 3.28)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE candidates RELATIVE "${root}"
  "${root}/include/*" "${root}/src/*" "${root}/apps/*" "${root}/tests/*"
  "${root}/benchmarks/*" "${root}/cmake/*" "${root}/docs/*" "${root}/qualification/*"
  "${root}/fuzz/*" "${root}/examples/*" "${root}/.github/*"
  "${root}/tools/plots/src/*.py" "${root}/tools/plots/tests/*.py")
list(APPEND candidates CMakeLists.txt README.md CONTRIBUTING.md local_ci.sh
  .clang-format .gitignore .gitattributes third_party/vallado_ad/SGP4.hpp
  tools/plots/README.md tools/plots/pyproject.toml)
set(count 0)
foreach(path IN LISTS candidates)
  # Immutable source/derived numerical fixtures have adjacent provenance;
  # historical CSV results and fuzz inputs cannot acquire comment records.
  if(path MATCHES "^tests/data/" AND NOT path MATCHES "\\.(md|cmake)$")
    continue()
  endif()
  if(path MATCHES "^docs/benchmarks/.*\\.csv$" OR path MATCHES "^fuzz/corpus/")
    continue()
  endif()
  # Strict request JSON has no comments; its purpose/author live in the adjacent README.
  if(path STREQUAL "examples/request.json")
    continue()
  endif()
  # Generated figures carry embedded metadata and adjacent captions/provenance.
  # Native snapshots and the manifest retain their strict JSON schemas.
  if(path MATCHES "^docs/figures/(residuals|serialization|iss-prediction)\\.(pdf|svg|png)$"
      OR path STREQUAL "docs/figures/manifest.json"
      OR path MATCHES "^docs/figures/input/(request|report)\\.json$")
    continue()
  endif()
  file(READ "${root}/${path}" prefix LIMIT 1024)
  if(NOT prefix MATCHES "@author watson" OR NOT prefix MATCHES "@brief [^\n]+")
    message(FATAL_ERROR "Missing top-of-file purpose/author header: ${path}")
  endif()
  math(EXPR count "${count} + 1")
endforeach()
file(READ "${root}/CMakePresets.json" presets)
string(JSON author GET "${presets}" vendor orbit-fit author)
string(JSON description GET "${presets}" vendor orbit-fit description)
if(NOT author STREQUAL "watson" OR description STREQUAL "")
  message(FATAL_ERROR "CMakePresets.json needs author and description metadata")
endif()
message(STATUS "Checked ${count} authored file headers and preset metadata")
