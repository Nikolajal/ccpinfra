cmake_minimum_required(VERSION 3.14)

project(cppinfra LANGUAGES CXX)

add_library(cppinfra
    src/rnd.cxx
)

target_include_directories(cppinfra
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(cppinfra PUBLIC cxx_std_17)

add_library(cppinfra::cppinfra ALIAS cppinfra)
