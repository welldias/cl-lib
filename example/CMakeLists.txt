add_executable(cl_example
    main.c
)

target_link_libraries(cl_example
    PRIVATE cl cl_warnings cl_sanitizers
)

add_executable(cl_tool
    tool.c
)

target_link_libraries(cl_tool
    PRIVATE cl cl_warnings cl_sanitizers
)

add_executable(cl_schema_example
    schema.c
)

target_link_libraries(cl_schema_example
    PRIVATE cl cl_warnings cl_sanitizers
)

add_executable(cl_callbacks_example
    callbacks.c
)

target_link_libraries(cl_callbacks_example
    PRIVATE cl cl_warnings cl_sanitizers
)
