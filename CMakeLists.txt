cmake_minimum_required(VERSION 3.15)
project(schemereload CXX)

add_library(schemereload SHARED schemereload.cpp)
target_compile_features(schemereload PRIVATE cxx_std_17)
# static CRT: no vcruntime dll needed next to tf_win64.exe
set_property(TARGET schemereload PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
