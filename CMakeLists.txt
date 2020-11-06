cmake_minimum_required(VERSION 3.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")
include(ConfigureAllModules) 

project(librevenge)

__required_find_package(boost)
add_subdirectory(zlib/external/ zlib)
add_subdirectory(src)

__configure_all()