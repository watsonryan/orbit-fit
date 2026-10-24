# @file dependencies.cmake
# @brief Resolve pinned dependencies or explicit preinstalled packages.
# @author watson
include_guard(GLOBAL)
if(ORBITFIT_FETCH_DEPS)
  include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)
  CPMAddPackage(NAME Eigen3 VERSION 3.4.0
    URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
    URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
    OPTIONS "BUILD_TESTING OFF" "EIGEN_BUILD_DOC OFF" "EIGEN_BUILD_PKGCONFIG OFF"
      "CMAKE_POLICY_VERSION_MINIMUM 3.5")
  if(Eigen3_ADDED)
    set(Eigen3_DIR "${Eigen3_BINARY_DIR}")
    # Eigen 3.4 exports this name on installation but omits its build-tree alias.
    if(NOT TARGET Eigen3::Eigen)
      add_library(Eigen3::Eigen ALIAS eigen)
    endif()
    # Load the generated version metadata for Ceres' exported dependency check.
    find_package(Eigen3 3.4 CONFIG REQUIRED PATHS "${Eigen3_BINARY_DIR}" NO_DEFAULT_PATH)
  endif()
  CPMAddPackage(NAME glog VERSION 0.7.1
    URL https://codeload.github.com/google/glog/tar.gz/refs/tags/v0.7.1
    URL_HASH SHA256=00e4a87e87b7e7612f519a41e491f16623b12423620006f59f5688bfd8d13b08
    OPTIONS "BUILD_TESTING OFF" "BUILD_SHARED_LIBS OFF" "WITH_GFLAGS OFF"
      "WITH_GTEST OFF" "WITH_UNWIND none" "WITH_PKGCONFIG OFF")
  if(TARGET glog)
    # Use a deterministic config adapter on the first AND subsequent configure.
    # glog's real build-tree target export is generated too late for the first
    # configure; a transient FetchContent redirect is absent on some cached runs.
    set(glog_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependency-config/glog")
    configure_file(${CMAKE_CURRENT_LIST_DIR}/glog-build-config.cmake.in
      "${glog_DIR}/glog-config.cmake" @ONLY)
    write_basic_package_version_file("${glog_DIR}/glog-config-version.cmake"
      VERSION 0.7.1 COMPATIBILITY SameMajorVersion)
  endif()
  if(TARGET glog::glog)
    # On case-insensitive filesystems CPM's Findglog.cmake also handles Glog.
    # Supply the legacy uppercase metadata it otherwise omits for Ceres 2.2.
    set(GLOG_FOUND TRUE)
    set(GLOG_LIBRARIES glog::glog)
    set(FOUND_INSTALLED_GLOG_CMAKE_CONFIGURATION TRUE)
    set(glog_VERSION 0.7.1)
  endif()
  # Dense QR needs no external sparse solver, BLAS, or GPU package.
  CPMAddPackage(NAME Ceres VERSION 2.2.0
    URL https://codeload.github.com/ceres-solver/ceres-solver/tar.gz/refs/tags/2.2.0
    URL_HASH SHA256=12efacfadbfdc1bbfa203c236e96f4d3c210bed96994288b3ff0c8e7c6f350d4
    OPTIONS "BUILD_TESTING OFF" "BUILD_EXAMPLES OFF" "BUILD_BENCHMARKS OFF"
      "BUILD_DOCUMENTATION OFF" "BUILD_SHARED_LIBS OFF" "MINIGLOG OFF" "GFLAGS OFF"
      "SUITESPARSE OFF" "EIGENSPARSE OFF" "EIGENMETIS OFF" "ACCELERATESPARSE OFF"
      "LAPACK OFF" "USE_CUDA OFF" "SCHUR_SPECIALIZATIONS OFF"
      "EXPORT_BUILD_DIR OFF" "PROVIDE_UNINSTALL_TARGET OFF")
  CPMAddPackage(NAME nlohmann_json VERSION 3.12.0
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    OPTIONS "JSON_BuildTests OFF" "JSON_Install ON")
  if(ORBITFIT_BUILD_CLI)
    # Logging belongs to the executable. The installed numerical library has no
    # spdlog dependency; the CLI statically links it and installs its notice.
    CPMAddPackage(NAME spdlog VERSION 1.17.0
      URL https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.17.0
      URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
      OPTIONS "SPDLOG_USE_STD_FORMAT ON" "SPDLOG_BUILD_SHARED OFF" "BUILD_SHARED_LIBS OFF"
        "SPDLOG_FMT_EXTERNAL OFF" "SPDLOG_FMT_EXTERNAL_HO OFF" "SPDLOG_INSTALL OFF"
        "SPDLOG_BUILD_ALL OFF" "SPDLOG_BUILD_EXAMPLE OFF" "SPDLOG_BUILD_EXAMPLE_HO OFF"
        "SPDLOG_BUILD_TESTS OFF" "SPDLOG_BUILD_TESTS_HO OFF" "SPDLOG_BUILD_BENCH OFF")
    if(spdlog_ADDED)
      target_compile_features(spdlog PRIVATE cxx_std_23)
    endif()
  endif()
  if(ORBITFIT_BUILD_TESTS)
    CPMAddPackage(NAME googletest VERSION 1.17.0
      URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0
      URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
      OPTIONS "INSTALL_GTEST OFF" "BUILD_GMOCK OFF" "BUILD_SHARED_LIBS OFF" "gtest_force_shared_crt ON")
  endif()
else()
  find_package(Eigen3 3.4 CONFIG REQUIRED)
  find_package(Ceres 2.2 CONFIG REQUIRED)
  find_package(nlohmann_json 3.12 CONFIG REQUIRED)
  if(ORBITFIT_BUILD_CLI)
    find_package(spdlog 1.17 CONFIG REQUIRED)
  endif()
  if(ORBITFIT_BUILD_TESTS)
    find_package(GTest 1.17 CONFIG REQUIRED)
  endif()
endif()
