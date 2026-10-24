# @file cli_logging.cmake
# @brief Verify logging severity, stderr routing, unchanged delivery, and failure exit semantics.
# @author watson
cmake_minimum_required(VERSION 3.28)
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${CLI}" example --output "${OUTPUT}/request.json"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
if(NOT status EQUAL 0 OR NOT output MATCHES "Wrote synthetic example" OR NOT diagnostics STREQUAL "")
  message(FATAL_ERROR "Example output must remain ordinary stdout: ${diagnostics}")
endif()

execute_process(COMMAND "${CLI}" fit --input "${OUTPUT}/request.json" --output-dir "${OUTPUT}/default"
  RESULT_VARIABLE status OUTPUT_VARIABLE baseline ERROR_VARIABLE diagnostics)
if(NOT status EQUAL 0 OR NOT baseline MATCHES "Accepted:" OR NOT diagnostics STREQUAL "")
  message(FATAL_ERROR "An accepted default run must have a result and quiet stderr: ${diagnostics}")
endif()

execute_process(COMMAND "${CLI}" fit --log-level debug --input "${OUTPUT}/request.json"
  --output-dir "${OUTPUT}/debug" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
string(FIND "${diagnostics}" "[debug]" debug_level)
string(FIND "${diagnostics}" "[info]" info_level)
string(FIND "${diagnostics}" "[trace]" trace_level)
string(ASCII 27 escape)
string(FIND "${diagnostics}" "${escape}" color)
if(NOT status EQUAL 0 OR debug_level EQUAL -1 OR info_level EQUAL -1 OR NOT trace_level EQUAL -1
    OR NOT color EQUAL -1 OR NOT diagnostics MATCHES "Z] \\[orbit-fit\\]"
    OR NOT diagnostics MATCHES "Validating serialized OMM and TLE products"
    OR NOT output MATCHES "Accepted:" OR output MATCHES "\\[(debug|info|trace)\\]")
  message(FATAL_ERROR "Debug logs must be UTC, filtered, uncolored on pipes, and confined to stderr: ${diagnostics}")
endif()
foreach(product elements.omm.json elements.tle report.json request.json)
  file(READ "${OUTPUT}/default/${product}" original)
  file(READ "${OUTPUT}/debug/${product}" logged)
  if(NOT original STREQUAL logged)
    message(FATAL_ERROR "Logging altered delivered ${product}")
  endif()
endforeach()

execute_process(COMMAND "${CLI}" fit --log-level off --input "${OUTPUT}/request.json"
  --output-dir "${OUTPUT}/off" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
if(NOT status EQUAL 0 OR NOT output MATCHES "Accepted:" OR NOT diagnostics STREQUAL "")
  message(FATAL_ERROR "Disabling diagnostics must preserve the command result")
endif()

# Every supported threshold must preserve error status and staging cleanup. Even
# critical/off suppress only diagnostics, never the actual operation's failure.
foreach(level trace debug info warn error critical off)
  execute_process(COMMAND "${CLI}" fit --input "${OUTPUT}/missing.json" --output-dir "${OUTPUT}/${level}-error"
    --log-level "${level}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
  file(GLOB leftovers "${OUTPUT}/${level}-error.tmp-*")
  if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR EXISTS "${OUTPUT}/${level}-error" OR leftovers)
    message(FATAL_ERROR "Logging changed failure/publication semantics at level ${level}")
  endif()
  string(FIND "${diagnostics}" "[error]" error_level)
  if(level STREQUAL "critical" OR level STREQUAL "off")
    if(NOT diagnostics STREQUAL "")
      message(FATAL_ERROR "${level} failed to suppress lower-severity diagnostics")
    endif()
  elseif(error_level EQUAL -1)
    message(FATAL_ERROR "Missing stderr error diagnostic at level ${level}")
  endif()
  string(FIND "${diagnostics}" "[trace]" trace_level)
  if(level STREQUAL "trace" AND trace_level EQUAL -1)
    message(FATAL_ERROR "Trace did not include staging diagnostics")
  endif()
endforeach()

file(READ "${OUTPUT}/request.json" request)
string(JSON tight SET "${request}" fit maximum_rms_position_m 1)
file(WRITE "${OUTPUT}/tight.json" "${tight}")
foreach(level warn error)
  execute_process(COMMAND "${CLI}" fit --input "${OUTPUT}/tight.json"
    --output-dir "${OUTPUT}/${level}-rejected" --log-level "${level}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
  if(NOT status EQUAL 2 OR NOT output MATCHES "Rejected; diagnostic candidate retained")
    message(FATAL_ERROR "Logging changed the rejection exit status or result")
  endif()
  string(FIND "${diagnostics}" "[warning]" warning_level)
  if(level STREQUAL "warn" AND warning_level EQUAL -1)
    message(FATAL_ERROR "Rejected delivery must produce a warning")
  elseif(level STREQUAL "error" AND NOT diagnostics STREQUAL "")
    message(FATAL_ERROR "Error threshold must suppress rejection warnings")
  endif()
endforeach()

# Invalid configuration fails before creating staging. A typo must never be
# silently interpreted as the valid off threshold.
foreach(extra "--log-level;verbose" "--log-level;off;--log-level;debug" "--log-level")
  execute_process(COMMAND "${CLI}" fit --input "${OUTPUT}/request.json" --output-dir "${OUTPUT}/invalid" ${extra}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
  string(FIND "${diagnostics}" "[error]" error_level)
  file(GLOB leftovers "${OUTPUT}/invalid.tmp-*")
  if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR error_level EQUAL -1
      OR EXISTS "${OUTPUT}/invalid" OR leftovers)
    message(FATAL_ERROR "Invalid logging options must report an error before staging")
  endif()
endforeach()
