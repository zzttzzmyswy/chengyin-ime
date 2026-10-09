# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(chengyin-ibus VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(IBUS REQUIRED IMPORTED_TARGET ibus-1.0)

get_filename_component(CHENGYIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CHENGYIN_RUST_LIBRARY "${CHENGYIN_ROOT}/target/release/libchengyin_ime.a" CACHE FILEPATH "Rust static library")
if(NOT EXISTS "${CHENGYIN_RUST_LIBRARY}")
  message(FATAL_ERROR "Run cargo build --release -p chengyin-ffi before CMake")
endif()

# engine.c calls ibus_keyval_to_unicode, so IBus is a PUBLIC dependency of the
# core. It is an imported target, so its include directories arrive as SYSTEM
# includes and -Werror never fires on a GLib header this project does not own.
add_library(chengyin-ibus-core STATIC engine.c)
set_target_properties(chengyin-ibus-core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(chengyin-ibus-core PUBLIC
  "${CHENGYIN_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(chengyin-ibus-core PRIVATE -Wall -Wextra -Werror)
target_link_libraries(chengyin-ibus-core PUBLIC PkgConfig::IBUS "${CHENGYIN_RUST_LIBRARY}"
  Threads::Threads ${CMAKE_DL_LIBS} m)

# The engine process. IBus spawns it as `ibus-engine-chengyin`; it statically links
# the Rust core, so the binary carries no libchengyin_ime.so dependency.
add_executable(ibus-engine-chengyin ibus_engine.c)
target_link_libraries(ibus-engine-chengyin PRIVATE chengyin-ibus-core PkgConfig::IBUS)
target_compile_options(ibus-engine-chengyin PRIVATE -Wall -Wextra -Werror)
set_target_properties(ibus-engine-chengyin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  INSTALL_RPATH "")

# Host-side driver for the headless end-to-end check (e2e.sh). A test fixture,
# not an installed product.
add_executable(chengyin-e2e-client e2e_client.c)
target_link_libraries(chengyin-e2e-client PRIVATE PkgConfig::IBUS)
target_compile_options(chengyin-e2e-client PRIVATE -Wall -Wextra -Werror)
set_target_properties(chengyin-e2e-client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

install(TARGETS ibus-engine-chengyin RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
install(FILES "${CHENGYIN_ROOT}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/ibus-chengyin" RENAME copyright)

if(BUILD_TESTING)
  add_executable(chengyin-ibus-test test_engine.c)
  target_link_libraries(chengyin-ibus-test PRIVATE chengyin-ibus-core)
  target_compile_options(chengyin-ibus-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME ibus-engine COMMAND chengyin-ibus-test)
  add_executable(chengyin-ibus-negative-test test_negative.c)
  target_link_libraries(chengyin-ibus-negative-test PRIVATE chengyin-ibus-core)
  target_compile_options(chengyin-ibus-negative-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME ibus-negative-control COMMAND chengyin-ibus-negative-test)
  set_tests_properties(ibus-engine ibus-negative-control PROPERTIES TIMEOUT 20)
endif()
