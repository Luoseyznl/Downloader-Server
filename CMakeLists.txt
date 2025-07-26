cmake_minimum_required(VERSION 3.28)

project(DownloaderApp VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(gflags REQUIRED)
# find_package(tbb REQUIRED)

include_directories(
    src/utils
    /usr/include/tbb
    /usr/include/gflags
)

file(GLOB_RECURSE SOURCES
    src/Downloader/*.cpp
    src/utils/*.cpp
    src/main.cpp
)

add_executable(DownloaderApp ${SOURCES})


target_link_libraries(DownloaderApp
    gflags
    tbb
    curl
)

# Optionally, link any required libraries here
# target_link_libraries(DownloaderApp <required_libraries>)