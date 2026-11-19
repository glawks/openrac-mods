cmake_minimum_required(VERSION 3.20)
project(challenge_mode_probe LANGUAGES C CXX)
find_package(OpenRACMod 1.12 REQUIRED)
openrac_add_mod(challenge_mode_probe MANIFEST openrac-mod.json
  SOURCES probe.cpp ../../../src/region_bindings.cpp)
target_include_directories(challenge_mode_probe PRIVATE ../../../src ../../../include)
target_compile_features(challenge_mode_probe PRIVATE cxx_std_17)
