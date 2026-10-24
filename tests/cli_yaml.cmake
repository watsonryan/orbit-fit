# @file cli_yaml.cmake
# @brief Verify YAML/CSV CLI examples, relative paths, preserved inputs, replay, and failed publication.
# @author watson
cmake_minimum_required(VERSION 3.28)
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}/unrelated-working-directory")
execute_process(COMMAND "${CLI}" example --output-dir "${OUTPUT}/example"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
if(NOT status EQUAL 0 OR NOT diagnostics STREQUAL "")
  message(FATAL_ERROR "YAML/CSV example generation failed: ${diagnostics}")
endif()
execute_process(COMMAND "${CLI}" fit --config "${OUTPUT}/example/fit.yaml" --output-dir "${OUTPUT}/fit"
  WORKING_DIRECTORY "${OUTPUT}/unrelated-working-directory"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
if(NOT status EQUAL 0 OR NOT output MATCHES "Accepted:" OR NOT diagnostics STREQUAL "")
  message(FATAL_ERROR "Relative CSV paths must resolve against the YAML directory: ${diagnostics}")
endif()
foreach(product elements.omm.json elements.tle report.json request.json input.yaml states.csv)
  if(NOT EXISTS "${OUTPUT}/fit/${product}")
    message(FATAL_ERROR "Missing YAML/CSV fit artifact: ${product}")
  endif()
endforeach()
file(READ "${OUTPUT}/example/fit.yaml" config)
file(READ "${OUTPUT}/fit/input.yaml" saved_config)
file(READ "${OUTPUT}/example/states.csv" csv)
file(READ "${OUTPUT}/fit/states.csv" saved_csv)
if(NOT config STREQUAL saved_config OR NOT csv STREQUAL saved_csv)
  message(FATAL_ERROR "Original YAML and CSV bytes must be preserved")
endif()

# Equivalent generated YAML/CSV and JSON requests must have the same fit/delivery.
execute_process(COMMAND "${CLI}" example --output "${OUTPUT}/inline.json" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CLI}" fit --input "${OUTPUT}/inline.json" --output-dir "${OUTPUT}/inline"
  COMMAND_ERROR_IS_FATAL ANY)
foreach(product elements.omm.json elements.tle report.json)
  file(READ "${OUTPUT}/fit/${product}" yaml_product)
  file(READ "${OUTPUT}/inline/${product}" json_product)
  if(NOT yaml_product STREQUAL json_product)
    message(FATAL_ERROR "YAML/CSV ingestion changed ${product}")
  endif()
endforeach()

# The resolved snapshot remains usable after the original external CSV is moved.
file(RENAME "${OUTPUT}/example/states.csv" "${OUTPUT}/example/moved.csv")
execute_process(COMMAND "${CLI}" fit --input "${OUTPUT}/fit/request.json" --output-dir "${OUTPUT}/replay"
  COMMAND_ERROR_IS_FATAL ANY)
file(READ "${OUTPUT}/fit/report.json" original)
file(READ "${OUTPUT}/replay/report.json" replay)
if(NOT original STREQUAL replay)
  message(FATAL_ERROR "Resolved JSON snapshot failed to reproduce the delivery report")
endif()
execute_process(COMMAND "${CLI}" fit --config "${OUTPUT}/example/fit.yaml" --output-dir "${OUTPUT}/missing-csv"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
file(GLOB leftovers "${OUTPUT}/missing-csv.tmp-*")
if(NOT status EQUAL 1 OR EXISTS "${OUTPUT}/missing-csv" OR leftovers OR NOT diagnostics MATCHES "states.csv")
  message(FATAL_ERROR "Missing external data must fail with its path and clean up staging")
endif()

execute_process(COMMAND "${CLI}" example --output-dir "${OUTPUT}/example"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 1)
  message(FATAL_ERROR "Example generation must preserve existing directories")
endif()
execute_process(COMMAND "${CLI}" fit --config "${OUTPUT}/example/fit.yaml"
  --input "${OUTPUT}/inline.json" --output-dir "${OUTPUT}/both"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
file(GLOB leftovers "${OUTPUT}/both.tmp-*")
if(NOT status EQUAL 1 OR EXISTS "${OUTPUT}/both" OR leftovers)
  message(FATAL_ERROR "Conflicting input modes must fail before staging")
endif()

# The repository's documented entry point must work independently of the generator.
execute_process(COMMAND "${CLI}" fit --config "${SOURCE}/examples/fit.yaml"
  --output-dir "${OUTPUT}/checked-in" WORKING_DIRECTORY "${OUTPUT}/unrelated-working-directory"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
if(NOT status EQUAL 0 OR NOT output MATCHES "Accepted:" OR NOT diagnostics STREQUAL "")
  message(FATAL_ERROR "Checked-in YAML/CSV example failed: ${diagnostics}")
endif()
