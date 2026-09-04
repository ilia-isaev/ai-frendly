cmake_minimum_required(VERSION 3.28)

project(
    hello
    VERSION 0.1.0
    DESCRIPTION "C++23 Hello World"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello
    src/main.cpp
)

target_sources(hello
    PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS src
    FILES src/app.cxx
)
