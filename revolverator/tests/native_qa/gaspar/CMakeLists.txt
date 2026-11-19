cmake_minimum_required(VERSION 3.20)
project(gaspar_helmet_probe LANGUAGES C CXX)
find_package(OpenRACMod 1.12 REQUIRED)
openrac_add_mod(gaspar_helmet_probe MANIFEST openrac-mod.json
  SOURCES probe.cpp ../../../src/region_bindings.cpp)
target_include_directories(gaspar_helmet_probe PRIVATE ../../../src)
target_compile_features(gaspar_helmet_probe PRIVATE cxx_std_17)
