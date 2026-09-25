add_executable(cl_tests
    main.c
    test_parser.c
    test_eval.c
    test_navigate.c
    test_writer.c
    test_fixtures.c
    test_bindings.c
    test_schema.c
    test_get.c
    test_export.c
    test_strings.c
)

target_link_libraries(cl_tests PRIVATE cl cl_warnings cl_sanitizers)
target_compile_definitions(cl_tests PRIVATE CL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/cl")

foreach(cl_test_group parser eval navigate writer fixtures bindings schema get export strings)
    add_test(NAME ${cl_test_group} COMMAND cl_tests ${cl_test_group})
endforeach()
