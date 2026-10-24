# @file reference.cmake
# @brief Reproduce the external Project Pluto comparison without linking its GPL code.
# @author watson
cmake_minimum_required(VERSION 3.28)
foreach(required REFERENCE_ROOT INPUT_DIR OUTPUT_DIR)
  if(NOT DEFINED ${required} OR NOT IS_ABSOLUTE "${${required}}")
    message(FATAL_ERROR "Pass an absolute -D${required}=... path")
  endif()
endforeach()
find_program(REFERENCE_MAKE NAMES gmake make REQUIRED)
find_program(REFERENCE_CXX NAMES clang++ g++ REQUIRED)
file(MAKE_DIRECTORY "${REFERENCE_ROOT}" "${OUTPUT_DIR}")

function(reference_source name revision sha)
  set(archive "${REFERENCE_ROOT}/${name}.tar.gz")
  if(NOT EXISTS "${archive}")
    file(DOWNLOAD "https://codeload.github.com/Bill-Gray/${name}/tar.gz/${revision}"
      "${archive}.download" EXPECTED_HASH SHA256=${sha} TLS_VERIFY ON STATUS download_status)
    list(GET download_status 0 download_code)
    if(NOT download_code EQUAL 0)
      message(FATAL_ERROR "Reference download failed: ${download_status}")
    endif()
    file(RENAME "${archive}.download" "${archive}")
  endif()
  file(SHA256 "${archive}" actual_sha)
  if(NOT actual_sha STREQUAL sha)
    message(FATAL_ERROR "Reference archive hash mismatch: ${archive}")
  endif()
  set(source "${REFERENCE_ROOT}/${name}-${revision}")
  if(NOT EXISTS "${source}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${REFERENCE_ROOT}")
  endif()
  set(${name}_SOURCE "${source}" PARENT_SCOPE)
endfunction()

reference_source(find_orb 1de55cca12e7a585c9c7384acbb80538ea2afc3c
  383418ecd2da8d2bf8e7dd13a667be0ed83318fc8997f2ea525eb0c71af949f0)
reference_source(sat_code 2814ea074e814d0f69b4ec1aab03425ea7a6d342
  f6e24d80d2134eac51c2ca2df8e7a73611531c6f2ea6d357439bfcb1588f8d7f)
reference_source(lunar 110ab53d46e3e5f1952318360e7bb01cb4884674
  1cee7eed8b4c1d6b717769782c5880e87bc8d246045bbbefed587a47ce946c6a)

execute_process(COMMAND "${REFERENCE_MAKE}" -j3 libsatell.a
  WORKING_DIRECTORY "${sat_code_SOURCE}" COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_FILE "${OUTPUT_DIR}/sat-code-build.log" ERROR_FILE "${OUTPUT_DIR}/sat-code-build.err")
execute_process(COMMAND "${REFERENCE_MAKE}" -j3 liblunar.a
  WORKING_DIRECTORY "${lunar_SOURCE}" COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_FILE "${OUTPUT_DIR}/lunar-build.log" ERROR_FILE "${OUTPUT_DIR}/lunar-build.err")
execute_process(COMMAND "${REFERENCE_MAKE}" -j3 eph2tle "CXX=${REFERENCE_CXX}"
  "CXXFLAGS=-c -O2 -std=c++23 -I\"${lunar_SOURCE}\" -I\"${sat_code_SOURCE}\""
  "LIBS=\"${lunar_SOURCE}/liblunar.a\" \"${sat_code_SOURCE}/libsatell.a\" -lm"
  WORKING_DIRECTORY "${find_orb_SOURCE}" COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_FILE "${OUTPUT_DIR}/eph2tle-build.log" ERROR_FILE "${OUTPUT_DIR}/eph2tle-build.err")

foreach(hours 6 12 24 48)
  math(EXPR count "${hours} * 15 + 1")
  math(EXPR midpoint "${hours} * 15 / 2")
  foreach(dimension 6 7)
    set(name "iss-${hours}h-${dimension}p")
    if(EXISTS "${OUTPUT_DIR}/${name}.tle")
      message(FATAL_ERROR "Use a fresh OUTPUT_DIR; refusing to overwrite ${name}.tle")
    endif()
    set(drag)
    if(dimension EQUAL 7)
      set(drag -b)
    endif()
    execute_process(COMMAND "${find_orb_SOURCE}/eph2tle" "${INPUT_DIR}/iss-${hours}h.eph"
      "-f${count}" "-x${midpoint}" -n25544 -i98067A -dISS -z80 -R1 ${drag}
      "-o${OUTPUT_DIR}/${name}.tle"
      WORKING_DIRECTORY "${find_orb_SOURCE}" COMMAND_ERROR_IS_FATAL ANY
      OUTPUT_FILE "${OUTPUT_DIR}/${name}.log" ERROR_FILE "${OUTPUT_DIR}/${name}.err")
  endforeach()
endforeach()
