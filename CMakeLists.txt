cmake_minimum_required(VERSION 3.16)

project(cl
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# True when cl is the main project, false when it is pulled in by a parent
# project through add_subdirectory() or FetchContent. (PROJECT_IS_TOP_LEVEL
# needs CMake 3.21.)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CL_IS_TOP_LEVEL ON)
else()
    set(CL_IS_TOP_LEVEL OFF)
endif()

# Global build settings are only applied when cl is the main project, so a
# parent project keeps control over its own output layout and build type.
if(CL_IS_TOP_LEVEL)
    # Put every executable and DLL in one directory so Windows can find cl.dll
    # when running the examples and the test suite.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()

# Examples and tests default to OFF when cl is consumed as a subproject.
option(CL_BUILD_EXAMPLES "Build the cl example programs" ${CL_IS_TOP_LEVEL})
option(CL_BUILD_TESTS "Build the cl test suite" ${CL_IS_TOP_LEVEL})
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

# Static analysis: `cmake --build <dir> --target cppcheck`. Settings live in
# cl.cppcheck; only options the project file cannot hold are passed here.
# Findings are written as plain text to <dir>/cppcheck-report.txt.
# Only defined for the main project so the target name cannot clash with a
# parent project's own "cppcheck" target.
if(CL_IS_TOP_LEVEL)
    find_program(CL_CPPCHECK_EXECUTABLE cppcheck)
endif()

if(CL_IS_TOP_LEVEL AND CL_CPPCHECK_EXECUTABLE)
    include(ProcessorCount)
    ProcessorCount(CL_CPPCHECK_JOBS)
    if(CL_CPPCHECK_JOBS EQUAL 0)
        set(CL_CPPCHECK_JOBS 1)
    endif()

    add_custom_target(cppcheck
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/build/cppcheck
        COMMAND ${CL_CPPCHECK_EXECUTABLE}
            --project=${PROJECT_SOURCE_DIR}/cl.cppcheck
            --std=c99
            --enable=warning,style,performance,portability,information
            --inline-suppr
            --error-exitcode=1
            -j${CL_CPPCHECK_JOBS}
            -q
            --output-file=${PROJECT_BINARY_DIR}/cppcheck-report.txt
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running cppcheck (report: ${PROJECT_BINARY_DIR}/cppcheck-report.txt)"
        VERBATIM
    )
endif()

add_subdirectory(src)

if(CL_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(CL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
