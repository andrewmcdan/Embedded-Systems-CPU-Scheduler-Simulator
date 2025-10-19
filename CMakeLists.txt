cmake_minimum_required(VERSION 3.20)
project(SchedulerSim LANGUAGES CXX)

# --- Project setup ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Include paths ---
include_directories(${PROJECT_SOURCE_DIR}/include)

# --- Dependencies ---
include(FetchContent)

# === nlohmann::json ===
# If not present, automatically fetch it into ./libs/json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/libs/json
)
FetchContent_MakeAvailable(json)

# === argparse ===
# Lightweight modern argument parser for C++
FetchContent_Declare(
  argparse
  GIT_REPOSITORY https://github.com/p-ranav/argparse.git
  GIT_TAG v2.9
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/libs/argparse
)
FetchContent_MakeAvailable(argparse)

# === spdlog ===
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/libs/spdlog
)
FetchContent_MakeAvailable(spdlog)

# === yaml-cpp ===
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/libs/yaml-cpp
)
FetchContent_MakeAvailable(yaml-cpp)

# Suppress MSVC "unsafe/deprecated" warnings (C4996)
# Suppress MSVC "unused parameter" warnings (C4100)
if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_C4100)
endif()

# --- Source files ---
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)

# --- Build executable ---
add_executable(scheduler_sim ${SRC_FILES})

# --- Link dependencies ---
target_link_libraries(scheduler_sim
  PRIVATE
    nlohmann_json::nlohmann_json
    argparse
    spdlog::spdlog_header_only
    yaml-cpp
)

target_include_directories(scheduler_sim SYSTEM PRIVATE
  ${spdlog_SOURCE_DIR}/include
)

# --- Compiler warnings ---
if (MSVC)
  target_compile_options(scheduler_sim PRIVATE /W4 /permissive-)
else()
  target_compile_options(scheduler_sim PRIVATE -Wall -Wextra -pedantic -Wconversion)
endif()

# --- Installation (optional) ---
install(TARGETS scheduler_sim RUNTIME DESTINATION bin)

# --- Status output ---
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Source dir: ${PROJECT_SOURCE_DIR}")

# --- Post-build: Copy data directory ---
if (EXISTS "${PROJECT_SOURCE_DIR}/data")
  add_custom_command(TARGET scheduler_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:scheduler_sim>/data"
    COMMENT "Copying data directory to output"
  )
endif()
