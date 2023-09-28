cmake_minimum_required(VERSION 3.13)
project(cariboulite_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    message("MSVC is not supported")
elseif (ANDROID)
    message("Android is not supported")
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(CARIBOULITE REQUIRED cariboulite)

    target_include_directories(cariboulite_source PRIVATE ${CARIBOULITE_INCLUDE_DIRS})
    target_link_directories(cariboulite_source PRIVATE ${CARIBOULITE_LIBRARY_DIRS})
    target_link_libraries(cariboulite_source PRIVATE ${CARIBOULITE_LIBRARIES})
endif ()