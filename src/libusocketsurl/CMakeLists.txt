cmake_minimum_required(VERSION 3.28)

project(
    libusocketsurl
    VERSION 0.1.0
    DESCRIPTION "Async HTTP GET using uSockets/libuv"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include(CTest)

enable_testing()

# Find libraries
find_library(LIB_USOCKETS NAMES uSockets HINTS ../../lib REQUIRED)
find_library(LIB_UV NAMES uv HINTS ../../lib REQUIRED)
find_library(LIB_SSL NAMES ssl REQUIRED)
find_library(LIB_CRYPTO NAMES crypto REQUIRED)

# Include directories (uSockets + libuv headers)
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Static library (C++23, conventional headers)
add_library(usurl STATIC src/usurl.cpp)

# Propaga o standard (C++23) como feature exportável ao install(EXPORT),
# para que consumidores com CMake >= 3.23 herdem o standard do target.
target_compile_features(usurl INTERFACE cxx_std_23)
#set_target_properties(usurl PROPERTIES CXX_STANDARD 23) # talvez melhor?

target_include_directories(usurl
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${PROJECT_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(usurl
    PUBLIC
    ${LIB_USOCKETS}
    ${LIB_UV}
    ${LIB_SSL}
    ${LIB_CRYPTO}
)

# Test executable
add_executable(usurl_tests
    tests/test_main.cpp
)

target_include_directories(usurl_tests
    PRIVATE
    ${PROJECT_INCLUDE_DIR}
)

target_link_libraries(usurl_tests
    PRIVATE
    usurl
)

add_test(NAME usurl_tests COMMAND usurl_tests)

install(TARGETS usurl
	EXPORT usurlTargets
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES src/usurl.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT usurlTargets
	FILE usurlTargets.cmake
	NAMESPACE usurl::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/usurl
)

configure_package_config_file(
	cmake/usurlConfig.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/usurlConfig.cmake
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/usurl
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/usurlConfig.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/usurl
)

