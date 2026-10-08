cmake_minimum_required(VERSION 3.16)
project(fe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses notcurses-core)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)

add_executable(fe
    main.cpp
    src/buffer.cpp
    src/config.cpp
    src/diff.cpp
    src/engine.cpp
    src/engine_edit.cpp
    src/engine_ui.cpp
    src/engine_render.cpp
    src/engine_search.cpp
    src/git.cpp
    src/log.cpp
    src/syntax.cpp
    src/workspace.cpp
    src/util/json_escape.cpp
    src/util/path_line_col.cpp
    src/util/base64.cpp
    src/util/indent.cpp
    src/util/bracket_match.cpp
    src/util/keymap_json.cpp
)

if(WIN32)
    target_link_libraries(fe PRIVATE gdi32 user32 ws2_32 winpthread)
endif()
target_include_directories(fe PRIVATE include)
target_link_libraries(fe PRIVATE PkgConfig::NOTCURSES PkgConfig::LIBGIT2 ${CMAKE_DL_LIBS})
