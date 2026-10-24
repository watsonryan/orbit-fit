# @file check_toolchain.cmake
# @brief Fail early when the selected compiler and standard library lack required C++23 facilities.
# @author watson
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
function(orbitfit_check_toolchain)
  cmake_push_check_state(RESET)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
  check_cxx_source_compiles("
    #include <expected>
    #include <format>
    #include <fstream>
    #include <print>
    #include <stop_token>
    #include <thread>
    int main() {
      std::expected<int, int> value = 23;
      const auto text = std::format(\"{}\", *value);
      const auto mode = std::ios::out | std::ios::noreplace;
      std::jthread worker([](std::stop_token) {});
      std::println(\"{}\", text);
      return text.empty() || mode == std::ios::openmode{};
    }" ORBITFIT_HAS_CXX23_PRINT_AND_IO)
  if(NOT ORBITFIT_HAS_CXX23_PRINT_AND_IO)
    message(FATAL_ERROR "orbit-fit needs C++23 compiler AND library support for expected, format, print, noreplace, and jthread. See docs/integration.md.")
  endif()
  cmake_pop_check_state()
endfunction()
orbitfit_check_toolchain()
