cmake_minimum_required(VERSION 3.20)
project(chengyin-fcitx5 VERSION 0.1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CTest)
find_package(Fcitx5Core 5.1 REQUIRED)
find_package(Threads REQUIRED)

# Fcitx exports no version macro to the preprocessor, so the source cannot tell
# the two incompatible StandardPath generations apart on its own. Encode the
# numeric form CMake already knows (5.1.12 -> 50112) for engine.cpp's #if.
if(NOT Fcitx5Core_VERSION)
  message(FATAL_ERROR "Fcitx5Core reports no version; cannot pick the StandardPath API generation")
endif()
string(REPLACE "." ";" CHENGYIN_FCITX_VERSION_PARTS "${Fcitx5Core_VERSION}")
list(GET CHENGYIN_FCITX_VERSION_PARTS 0 CHENGYIN_FCITX_VERSION_MAJOR)
list(GET CHENGYIN_FCITX_VERSION_PARTS 1 CHENGYIN_FCITX_VERSION_MINOR)
list(LENGTH CHENGYIN_FCITX_VERSION_PARTS CHENGYIN_FCITX_VERSION_COUNT)
if(CHENGYIN_FCITX_VERSION_COUNT GREATER 2)
  list(GET CHENGYIN_FCITX_VERSION_PARTS 2 CHENGYIN_FCITX_VERSION_PATCH)
else()
  set(CHENGYIN_FCITX_VERSION_PATCH 0)
endif()
math(EXPR CHENGYIN_FCITX_VERSION
  "${CHENGYIN_FCITX_VERSION_MAJOR} * 10000 + ${CHENGYIN_FCITX_VERSION_MINOR} * 100 + ${CHENGYIN_FCITX_VERSION_PATCH}")
get_filename_component(CHENGYIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CHENGYIN_RUST_LIBRARY "${CHENGYIN_ROOT}/target/release/libchengyin_ime.a" CACHE FILEPATH "Rust static library")
if(NOT EXISTS "${CHENGYIN_RUST_LIBRARY}")
  message(FATAL_ERROR "Run cargo build --release -p chengyin-ffi before CMake")
endif()
# dictionary_loader has no Fcitx dependency; it lives in platforms/common so the
# IBus engine reuses the same loading semantics.
set(CHENGYIN_COMMON_DIR "${CHENGYIN_ROOT}/platforms/common")
# The regression test copies this lexicon to a private path, so it can exercise
# the full-vocabulary default without writing to an installed file.
set(CHENGYIN_TEST_DICTIONARY "${CHENGYIN_ROOT}/data/daily.tsv")
add_library(chengyin-engine STATIC engine.cpp "${CHENGYIN_COMMON_DIR}/dictionary_loader.cpp")
set_target_properties(chengyin-engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(chengyin-engine PUBLIC cxx_std_17)
target_include_directories(chengyin-engine PUBLIC "${CHENGYIN_ROOT}/include" "${CHENGYIN_COMMON_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(chengyin-engine PRIVATE CHENGYIN_FCITX_VERSION=${CHENGYIN_FCITX_VERSION})
# Packaging mode. A distribution install ships the full lexicon and compiles its
# installed location in as the default, so a fresh user needs no manual step. Both
# halves derive from this one switch, so the path the module loads and the file
# that gets installed cannot drift apart. A plain source build leaves it off and
# keeps the bundled demo dictionary.
set(CHENGYIN_PACKAGED_DATA OFF CACHE BOOL
  "Install the full lexicon and make it the default for new profiles")
if(CHENGYIN_PACKAGED_DATA)
  set(CHENGYIN_LEXICON_DIR "${CMAKE_INSTALL_DATADIR}/chengyin")
  set(CHENGYIN_DEFAULT_DICTIONARY_PATH "${CMAKE_INSTALL_FULL_DATADIR}/chengyin/daily.tsv")
else()
  set(CHENGYIN_DEFAULT_DICTIONARY_PATH "")
endif()
# PUBLIC, not PRIVATE: the default argument lives in engine.h, so every unit that
# includes it - including factory.cpp, which builds the addon - must see the same
# value. A PRIVATE definition would leave the plugin on the demo default.
target_compile_definitions(chengyin-engine PUBLIC
  "CHENGYIN_DEFAULT_DICTIONARY_PATH=\"${CHENGYIN_DEFAULT_DICTIONARY_PATH}\"")
target_compile_options(chengyin-engine PRIVATE -Wall -Wextra -Werror)
target_link_libraries(chengyin-engine PUBLIC Fcitx5::Core Fcitx5::Config "${CHENGYIN_RUST_LIBRARY}" Threads::Threads ${CMAKE_DL_LIBS} m)

add_library(chengyin MODULE factory.cpp)
set_target_properties(chengyin PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
target_link_libraries(chengyin PRIVATE chengyin-engine)
target_link_options(chengyin PRIVATE "-Wl,--exclude-libs,ALL" "-Wl,--no-undefined")
set(CHENGYIN_ADDON_DIR "${FCITX_INSTALL_ADDONDIR}" CACHE PATH "Fcitx5 addon directory from distro SDK")
install(TARGETS chengyin LIBRARY DESTINATION "${CHENGYIN_ADDON_DIR}")
install(FILES chengyin-addon.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon" RENAME chengyin.conf)
install(FILES chengyin-inputmethod.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod" RENAME chengyin.conf)
install(FILES "${CHENGYIN_ROOT}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/fcitx5-chengyin" RENAME copyright)
install(FILES "${CHENGYIN_ROOT}/packaging/debian/README.Debian" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/fcitx5-chengyin")
if(CHENGYIN_PACKAGED_DATA)
  install(FILES "${CHENGYIN_ROOT}/data/daily.tsv"
          DESTINATION "${CHENGYIN_LEXICON_DIR}")
  install(FILES "${CHENGYIN_ROOT}/data/README.md"
          DESTINATION "${CHENGYIN_LEXICON_DIR}" RENAME DICTIONARY.md)
  # Upstream licenses and fixed-revision provenance records: the redistributed
  # lexicon must carry its attribution, and its data license stays separate from
  # the code's MIT license.
  foreach(_source jieba rime-pinyin-simp thuocl phrase-pinyin-data)
    install(FILES "${CHENGYIN_ROOT}/data/sources/${_source}/LICENSE"
            DESTINATION "${CHENGYIN_LEXICON_DIR}/licenses" RENAME LICENSE.${_source})
    install(FILES "${CHENGYIN_ROOT}/data/sources/${_source}/SOURCE.json"
            DESTINATION "${CHENGYIN_LEXICON_DIR}/licenses" RENAME SOURCE.${_source}.json)
    install(FILES "${CHENGYIN_ROOT}/data/sources/${_source}/README.md"
            DESTINATION "${CHENGYIN_LEXICON_DIR}/licenses" RENAME README.${_source}.md)
  endforeach()
  # The lexicon path compiled into the module must match what actually landed.
  if(NOT CHENGYIN_DEFAULT_DICTIONARY_PATH STREQUAL "${CMAKE_INSTALL_FULL_DATADIR}/chengyin/daily.tsv")
    message(FATAL_ERROR "packaged default and installed lexicon disagree")
  endif()
endif()

if(BUILD_TESTING)
  add_executable(chengyin-fcitx5-test test_engine.cpp)
  target_link_libraries(chengyin-fcitx5-test PRIVATE chengyin-engine)
  target_compile_options(chengyin-fcitx5-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME fcitx5-engine COMMAND chengyin-fcitx5-test)
  add_executable(chengyin-reload-test test_dictionary_reload.cpp)
  target_link_libraries(chengyin-reload-test PRIVATE chengyin-engine)
  target_compile_options(chengyin-reload-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME fcitx5-dictionary-reload COMMAND chengyin-reload-test)
  add_executable(chengyin-default-dictionary-test test_default_dictionary.cpp)
  target_link_libraries(chengyin-default-dictionary-test PRIVATE chengyin-engine)
  # The regression loads a private copy of the shipped lexicon, so it proves the
  # full-vocabulary default without writing to the installed path.
  target_compile_definitions(chengyin-default-dictionary-test PRIVATE
    "CHENGYIN_TEST_DICTIONARY=\"${CHENGYIN_TEST_DICTIONARY}\"")
  target_compile_options(chengyin-default-dictionary-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  add_test(NAME fcitx5-default-dictionary COMMAND chengyin-default-dictionary-test)
  set_tests_properties(fcitx5-engine fcitx5-dictionary-reload
    PROPERTIES TIMEOUT 15)
  set_tests_properties(fcitx5-default-dictionary PROPERTIES TIMEOUT 30)
endif()
