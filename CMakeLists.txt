# Downloader Project CMakeLists.txt
# Manages build configuration for the multi-threaded downloader application

# Require a modern CMake version
cmake_minimum_required(VERSION 3.14)

# Define the project with version information
project(Downloader 
        VERSION 1.0.0
        DESCRIPTION "Multi-threaded download manager with server-client architecture"
        LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

# Find required packages
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)  # For std::thread

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Define source files for each component
set(THREADPOOL_SOURCES
    ThreadPool/ThreadPool.h
    ThreadPool/ThreadPool.cpp
)

set(LOGGER_SOURCES
    Logger/LoggerManager.h
)

set(DOWNLOADER_CORE_SOURCES
    DownloaderCore/DownloadTask.h
    DownloaderCore/DownloadTask.cpp
    DownloaderCore/Downloader.h
    DownloaderCore/Downloader.cpp
)

# Create a static library for the core components
add_library(downloader_core STATIC
    ${THREADPOOL_SOURCES}
    ${LOGGER_SOURCES}
    ${DOWNLOADER_CORE_SOURCES}
)

# Set library properties
target_link_libraries(downloader_core
    PRIVATE
        ${CURL_LIBRARIES}
        spdlog::spdlog
        Threads::Threads
)

# Create server executable
add_executable(server server.cpp)
target_link_libraries(server 
    PRIVATE 
        downloader_core
        ${CURL_LIBRARIES}
        spdlog::spdlog
        Threads::Threads
)

# Create client executable
add_executable(client client.cpp)
target_link_libraries(client
    PRIVATE
        spdlog::spdlog
        Threads::Threads
)

# Create logs directory if it doesn't exist
add_custom_command(
    TARGET server PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/logs
)
add_custom_command(
    TARGET client PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/logs
)

# Create download directory if it doesn't exist
add_custom_command(
    TARGET server PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/downloads
)
add_custom_command(
    TARGET client PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/downloads
)

# Install targets
install(TARGETS server client
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Print build information
message(STATUS "CURL libraries: ${CURL_LIBRARIES}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")