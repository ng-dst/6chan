cmake_minimum_required(VERSION 3.26)
project(lab6 C)

set(CMAKE_C_STANDARD 23)

# Debug build:
#   - slower poll rate (5000ms vs 300ms)
#   - logging in stderr
# add_definitions(-DDEBUG)

# Use named pipes
set(USE_PIPES TRUE)


if (USE_PIPES)
    add_definitions(-DUSE_PIPES)
endif()

add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(utils)

