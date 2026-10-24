<!-- @brief Document supported toolchains, CPM consumption, relocated installations, and package verification. @author watson -->
# Build and integrate orbit-fit

The library and CLI are native C++23. CMake 3.28 or newer is required. The compiler **and its standard library** must provide `std::expected`, `std::format`, `std::print`/`std::println`, `std::ios::noreplace`, `std::jthread`, and stop tokens. Configuration compiles and links a small feature probe before downloading dependencies. Selecting the C++23 language flag alone is insufficient. Command results use the C++23 printing functions and CLI diagnostics use spdlog; file streams and string streams serve file I/O and parsing. GCC 14 provides printing on the targeted Linux platform; its [release notes](https://gcc.gnu.org/gcc-14/changes.html) describe platform-specific link requirements.

## Platform evidence and CI scope

| Platform | Compiler/library target | Verification |
|---|---|---|
| macOS arm64 | AppleClang 21 / accompanying libc++ | Local Release and sanitizer builds; see the dated validation record |
| Ubuntu 24.04 | GCC 14 / libstdc++ 14 | CI Release and sanitizer jobs; local arm64 container results recorded separately |
| Windows 2022 runner, x64 | Visual Studio 2022 / its MSVC standard library | CI Release, installation, header, and consumer checks configured; execution evidence pending |

The exact compiler and dependency versions are included in each fit report. CI runner names are targets, not evidence of a completed hosted run. See [validation](validation.md) for what was actually executed and the [GitHub workflow results](https://github.com/watsonryan/orbit-fit/actions) for hosted checks.

The CLI's exclusive publication uses Linux `renameat2(RENAME_NOREPLACE)`, macOS `renamex_np(RENAME_EXCL)`, and Windows `MoveFileExW` without replacement flags. Unsupported filesystems return an I/O error. There is no fallback that could replace an existing destination. Other operating systems need an implementation of this operation before enabling the CLI.

## Consume the source with CPM

Use your existing hash-pinned CPM bootstrap. For a local checkout, this is the complete dependency declaration:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_orbit_app LANGUAGES C CXX)
include(cmake/CPM.cmake)
CPMAddPackage(
  NAME orbitfit
  SOURCE_DIR /absolute/path/to/orbit-fit
  OPTIONS "ORBITFIT_BUILD_TESTS OFF" "ORBITFIT_BUILD_CLI OFF"
)
add_executable(my_orbit_app main.cpp)
target_link_libraries(my_orbit_app PRIVATE orbitfit::orbitfit)
```

For GitHub consumption, replace `SOURCE_DIR` with `GITHUB_REPOSITORY watsonryan/orbit-fit` and an immutable `GIT_TAG` commit, or a release `URL` with `URL_HASH SHA256=...`. The target supplies its C++23 requirement and include directories; consumers do not add Ceres or Eigen headers to the orbit-fit interface. Source integration uses the project's pinned dependencies by default.

The [CPM consumer](../tests/cpm_consumer/CMakeLists.txt) is an executable integration check:

```sh
cmake -S tests/cpm_consumer -B build/cpm-consumer -G Ninja \
  -DORBITFIT_SOURCE_DIR="$PWD" -DCMAKE_BUILD_TYPE=Release \
  -DCPM_SOURCE_CACHE="$PWD/.cache/cpm"
cmake --build build/cpm-consumer --parallel 4
ctest --test-dir build/cpm-consumer --output-on-failure
```

## Consume an installation

Build and install the pinned dependency bundle into a new prefix:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
cmake --install build/release --prefix /absolute/path/to/orbitfit-install
```

The installed CMake package is used without loading the producing build's cache:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_orbit_app LANGUAGES CXX)
find_package(orbitfit 0.1 CONFIG REQUIRED)
add_executable(my_orbit_app main.cpp)
target_link_libraries(my_orbit_app PRIVATE orbitfit::orbitfit)
```

Configure with `-DCMAKE_PREFIX_PATH=/absolute/path/to/orbitfit-install`. The static library retains Ceres, Eigen, glog, and JSON package dependencies. The default CPM build installs them alongside orbit-fit. The CLI also privately links pinned spdlog using `std::format` and yaml-cpp for YAML input; library-only builds and installed library consumers do not need either. With `ORBITFIT_FETCH_DEPS=OFF`, supply compatible dependencies yourself through explicit prefixes, including spdlog 1.17+ and yaml-cpp 0.9+ if building the CLI; installation does not copy externally supplied packages. Keep compiler ABI, architecture, runtime library, and sanitizer choices compatible. Sanitized installations propagate their link-time sanitizer requirement to consumers.

The `package-check` target qualifies the self-contained installation produced by the default CPM build. It installs into a fresh owned directory, moves the prefix, scans exported CMake files for source/build/cache paths, disables user package registries, audits imported dependency paths, compiles every installed public header independently, and builds/runs a consumer of fitting, delivery, reporting, and curvature:

```sh
cmake --build --preset release --target package-check
# Or configure, build, test, and check the package together:
./local_ci.sh release
./local_ci.sh sanitize
```

A system-dependency build needs those dependency installations included in its deployment bundle to satisfy this self-contained check. The script intentionally fails if a developer's unrelated installation is needed to make the bundle work.

CI uploads the resulting `orbitfit-package.tar.gz` and consumes it on a **separate runner without checkout or dependency cache restoration**. The consumer has only the installed archive and its own small example source; the producing source/build trees are absent. Windows runs this same install/relocation check. Archives from this workflow are verification artifacts, not a general ABI-compatible binary release promise.

## Reproducible offline work

CPM archive URLs and SHA-256 hashes live in [dependencies](dependencies.md). After populating the source cache, ordinary tests and frozen forecast examples do not download orbital data. `ORBITFIT_FETCH_DEPS=OFF` disables dependency fetching when compatible packages are already installed. Documentation uses Doxygen only; tests, examples, derivatives, and qualification tools do not require Python.

If macOS Doxygen 1.18.0 crashes with `SIGBUS` in `commentscanYYlex`, compare the [upstream packaging report](https://github.com/doxygen/doxygen/issues/12326). Both the Homebrew and official arm64 binaries have exhibited this scanner crash locally; an earlier successful official-binary run did not establish a reliable workaround. Linux Doxygen 1.9.8 passes the current documentation and is also used by CI. Select a specific executable with `-DDOXYGEN_EXECUTABLE=/absolute/path/to/doxygen`; the tested release archive digest and results are recorded in [validation](validation.md). Keep documentation warnings fatal.

When configuration reports missing C++23 features, fix the compiler/standard-library pair first. When `find_package` cannot find Ceres or glog, inspect the installation prefix and dependency configuration rather than adding build-tree include paths. See [CMake's relocatable package guidance](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html#creating-relocatable-packages).
