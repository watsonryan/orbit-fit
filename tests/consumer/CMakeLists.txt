# @file CMakeLists.txt
# @brief Consume a relocated installed package without borrowing the producing build's cache.
# @author watson
cmake_minimum_required(VERSION 3.28)
project(orbitfit_consumer LANGUAGES CXX)
find_package(orbitfit 0.1 CONFIG REQUIRED)

# In package qualification every imported third-party header/library must resolve
# within the explicitly supplied prefix. No registry or developer installation may
# silently rescue an incomplete archive. Threads::Threads has no filesystem path.
if(ORBITFIT_ALLOWED_PREFIX)
  file(REAL_PATH "${ORBITFIT_ALLOWED_PREFIX}" allowed)
  get_property(imported DIRECTORY PROPERTY IMPORTED_TARGETS)
  foreach(target IN LISTS imported)
    get_target_property(configs "${target}" IMPORTED_CONFIGURATIONS)
    set(properties INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
      INTERFACE_LINK_DIRECTORIES INTERFACE_LINK_LIBRARIES IMPORTED_LOCATION IMPORTED_IMPLIB)
    foreach(config IN LISTS configs)
      list(APPEND properties "IMPORTED_LOCATION_${config}" "IMPORTED_IMPLIB_${config}")
    endforeach()
    foreach(property IN LISTS properties)
      get_target_property(paths "${target}" "${property}")
      foreach(path IN LISTS paths)
        if(IS_ABSOLUTE "${path}")
          file(REAL_PATH "${path}" resolved)
          cmake_path(IS_PREFIX allowed "${resolved}" NORMALIZE inside)
          if(NOT inside)
            message(FATAL_ERROR "${target}.${property} escapes the installed prefix: ${path}")
          endif()
        endif()
      endforeach()
    endforeach()
  endforeach()
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE orbitfit::orbitfit)
set_target_properties(consumer PROPERTIES CXX_EXTENSIONS OFF)
# Compile each public header first in its own translation unit.
foreach(header types orbitfit products io curvature)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/header-${header}.cpp" "#include <orbitfit/${header}.hpp>\n")
  list(APPEND header_checks "${CMAKE_CURRENT_BINARY_DIR}/header-${header}.cpp")
endforeach()
add_library(installed_headers OBJECT ${header_checks})
target_link_libraries(installed_headers PRIVATE orbitfit::orbitfit)
enable_testing()
add_test(NAME installed_consumer COMMAND consumer)
