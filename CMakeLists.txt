cmake_minimum_required(VERSION 3.16)
project(fe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses notcurses-core)

add_executable(fe
    main.cpp
    src/buffer.cpp
    src/config.cpp
    src/diff.cpp
    src/engine.cpp
    src/git.cpp
    src/log.cpp
    src/syntax.cpp
)

target_include_directories(fe PRIVATE include)
target_link_libraries(fe PRIVATE PkgConfig::NOTCURSES ${CMAKE_DL_LIBS})