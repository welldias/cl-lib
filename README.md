# cl

`cl` is a small C99 library for parsing and evaluating an HCL-like
configuration language. It is deliberately **agnostic**: no identifier —
`var`, `resource`, `provider`, `locals`, or anything else — has built-in
meaning. Every name in a `.cl` file is just a name; any convention on top
of that (Terraform-style or otherwise) is up to the author of the file, not
the library.

## Design: two independent phases

1. **Load** (`cl_load_file` / `cl_load_string`) parses source text into a
   purely structural AST (`cl_document_t`) — attributes, blocks, literals,
   operators, templates, and so on. Nothing is resolved at this stage.
2. **Evaluate** (`cl_document_evaluate`) is a separate, opt-in pass that
   walks that AST and resolves every expression — including traversals,
   operators, function calls, and templates — into a plain value tree
   (`cl_evaluated_t`). It never mutates the original document, and it never
   special-cases any name.

You can use phase 1 on its own (to read, mutate, or serialize the literal
structure) without ever calling phase 2.

## Requirements

- A C99 compiler
- CMake

## Building

```sh
cmake -S . -B build
cmake --build build
```

Build options (all set on `CMakeLists.txt` / `src/CMakeLists.txt`):

| Option                  | Default | Effect                                           |
|--------------------------|---------|---------------------------------------------------|
| `CL_BUILD_EXAMPLES`      | `ON`    | Build `cl_example` and `cl_tool`                   |
| `CL_BUILD_TESTS`         | `ON`    | Build and register the `cl_tests` CTest suite      |
| `CL_BUILD_STATIC`        | `ON`    | Build the static library (`libcl.a`)               |
| `CL_BUILD_SHARED`        | `ON`    | Build the shared library (`libcl.so`)              |
| `CL_ENABLE_SANITIZERS`   | `OFF`   | Instrument everything with ASan + UBSan            |

To build with sanitizers enabled:

```sh
cmake -S . -B build-asan -DCL_ENABLE_SANITIZERS=ON
cmake --build build-asan
```

## Running the tests

```sh
ctest --test-dir build
```

This runs the `cl_tests` suite (in `tests/`) as 5 named CTest cases:
`parser`, `eval`, `navigate`, `writer`, and `fixtures`.

## License

MIT — see [LICENSE](LICENSE).
