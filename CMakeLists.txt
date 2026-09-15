cmake_minimum_required(VERSION 4.0)

project(cl
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CL_BUILD_EXAMPLES "Build the cl example programs" ON)
option(CL_BUILD_TESTS "Build the cl test suite" ON)
option(CL_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Treat every compiler warning as an error across the whole project.
add_library(cl_warnings INTERFACE)

if(MSVC)
    target_compile_options(cl_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(cl_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# Opt-in ASan+UBSan instrumentation, shared by the library, the examples and
# the test suite so a single flag rebuilds everything consistently.
add_library(cl_sanitizers INTERFACE)

if(CL_ENABLE_SANITIZERS)
    if(MSVC)
        message(WARNING "CL_ENABLE_SANITIZERS has no effect with MSVC; ignoring.")
    else()
        target_compile_options(cl_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(cl_sanitizers INTERFACE -fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(src)

if(CL_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(CL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
