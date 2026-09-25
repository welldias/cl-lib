set(CL_SOURCES
    cl.c
    cl_arena.c
    cl_ast.c
    cl_lexer.c
    cl_template.c
    cl_parser.c
    cl_navigate.c
    cl_mutate.c
    cl_writer.c
    cl_eval.c
    cl_functions.c
    cl_bindings.c
    cl_schema.c
    cl_schema_load.c
    cl_get.c
    cl_export.c
)

option(CL_BUILD_STATIC "Build the static cl library (libcl.a)" ON)
option(CL_BUILD_SHARED "Build the shared cl library (libcl.so)" ON)

# cl_eval.c uses fmod() for the "%" operator.
if(UNIX)
    find_library(CL_MATH_LIBRARY m)
endif()

if(NOT CL_BUILD_STATIC AND NOT CL_BUILD_SHARED)
    message(FATAL_ERROR "At least one of CL_BUILD_STATIC or CL_BUILD_SHARED must be ON")
endif()

if(CL_BUILD_STATIC)
    add_library(cl_static STATIC ${CL_SOURCES})
    target_include_directories(cl_static PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(cl_static PRIVATE cl_warnings cl_sanitizers)
    if(CL_MATH_LIBRARY)
        target_link_libraries(cl_static PRIVATE ${CL_MATH_LIBRARY})
    endif()
    # On Windows the DLL's import library is also cl.lib, so the static one
    # needs a distinct name to avoid clobbering it.
    if(WIN32)
        set_target_properties(cl_static PROPERTIES OUTPUT_NAME cl_static)
    else()
        set_target_properties(cl_static PROPERTIES OUTPUT_NAME cl)
    endif()
endif()

if(CL_BUILD_SHARED)
    add_library(cl_shared SHARED ${CL_SOURCES})
    target_include_directories(cl_shared PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(cl_shared PRIVATE cl_warnings cl_sanitizers)
    if(CL_MATH_LIBRARY)
        target_link_libraries(cl_shared PRIVATE ${CL_MATH_LIBRARY})
    endif()
    # The public header has no dllexport annotations; export everything so
    # MSVC produces an import library for the DLL.
    set_target_properties(cl_shared PROPERTIES
        OUTPUT_NAME cl
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# `cl` is the default target other parts of the build (e.g. the example) link
# against: shared when available, falling back to static otherwise.
if(CL_BUILD_SHARED)
    add_library(cl ALIAS cl_shared)
else()
    add_library(cl ALIAS cl_static)
endif()
