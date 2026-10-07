cmake_minimum_required(VERSION 3.20)
project(myswy-ibus VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(IBUS REQUIRED IMPORTED_TARGET ibus-1.0)

get_filename_component(MYSWY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MYSWY_RUST_LIBRARY "${MYSWY_ROOT}/target/release/libmyswy_ime.a" CACHE FILEPATH "Rust static library")
if(NOT EXISTS "${MYSWY_RUST_LIBRARY}")
  message(FATAL_ERROR "Run cargo build --release -p myswy-ffi before CMake")
endif()

# engine.c calls ibus_keyval_to_unicode, so IBus is a PUBLIC dependency of the
# core. It is an imported target, so its include directories arrive as SYSTEM
# includes and -Werror never fires on a GLib header this project does not own.
add_library(myswy-ibus-core STATIC engine.c)
set_target_properties(myswy-ibus-core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(myswy-ibus-core PUBLIC
  "${MYSWY_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(myswy-ibus-core PRIVATE -Wall -Wextra -Werror)
target_link_libraries(myswy-ibus-core PUBLIC PkgConfig::IBUS "${MYSWY_RUST_LIBRARY}"
  Threads::Threads ${CMAKE_DL_LIBS} m)

# The engine process. IBus spawns it as `ibus-engine-myswy`; it statically links
# the Rust core, so the binary carries no libmyswy_ime.so dependency.
add_executable(ibus-engine-myswy ibus_engine.c)
target_link_libraries(ibus-engine-myswy PRIVATE myswy-ibus-core PkgConfig::IBUS)
target_compile_options(ibus-engine-myswy PRIVATE -Wall -Wextra -Werror)
set_target_properties(ibus-engine-myswy PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  INSTALL_RPATH "")

# Host-side driver for the headless end-to-end check (e2e.sh). A test fixture,
# not an installed product.
add_executable(myswy-e2e-client e2e_client.c)
target_link_libraries(myswy-e2e-client PRIVATE PkgConfig::IBUS)
target_compile_options(myswy-e2e-client PRIVATE -Wall -Wextra -Werror)
set_target_properties(myswy-e2e-client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

install(TARGETS ibus-engine-myswy RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
install(FILES "${MYSWY_ROOT}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/ibus-myswy" RENAME copyright)

if(BUILD_TESTING)
  add_executable(myswy-ibus-test test_engine.c)
  target_link_libraries(myswy-ibus-test PRIVATE myswy-ibus-core)
  target_compile_options(myswy-ibus-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME ibus-engine COMMAND myswy-ibus-test)
  add_executable(myswy-ibus-negative-test test_negative.c)
  target_link_libraries(myswy-ibus-negative-test PRIVATE myswy-ibus-core)
  target_compile_options(myswy-ibus-negative-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME ibus-negative-control COMMAND myswy-ibus-negative-test)
  set_tests_properties(ibus-engine ibus-negative-control PROPERTIES TIMEOUT 20)
endif()
