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

This runs the `cl_tests` suite (in `tests/`) as 6 named CTest cases:
`parser`, `eval`, `navigate`, `writer`, `fixtures`, and `bindings`.

## Quick start

```c
#include "cl/cl.h"
#include <stdio.h>

int main(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "name = \"Ada\"\n"
        "greeting = \"Hello, ${upper(name)}!\"\n",
        "example", &err);
    if (!doc) {
        fprintf(stderr, "parse error (%d:%d): %s\n", err.line, err.col, err.message);
        return 1;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    if (!result) {
        fprintf(stderr, "eval error (%d:%d): %s\n", err.line, err.col, err.message);
        cl_document_free(doc);
        return 1;
    }

    cl_evaluated_attribute_t *greeting = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "greeting");
    printf("%s\n", cl_value_as_string(greeting->value)); /* Hello, ADA! */

    cl_evaluated_free(result);
    cl_document_free(doc);
    return 0;
}
```

The entire public API lives in `include/cl/cl.h`. See `example/cl_tool.c`
for a fuller example that dumps both the raw AST and the evaluated tree of
any `.cl` file.

## Syntax tour

**Attributes and blocks**

```hcl
name = "Ada"

server "web" {
  port = 8080
}
```

**Objects, lists/tuples**

```hcl
person = {
  first_name = "Ada"
  last_name  = "Lovelace"
}

ips = ["127.0.0.1", "192.168.0.1"]
```

**Traversal — `.attr`, `[index]`, splat `.*` / `[*]`**

```hcl
region = provider.aws.region
first  = list[0]
all_ports = servers[*].port
```

**Operators and conditionals**

```hcl
total     = 1 + 2 * 3        # arithmetic; usual precedence, "*" before "+"
is_prod   = env == "production" && enabled
label     = is_prod ? "PROD" : "dev"
```

**For-expressions**

```hcl
doubled = [for v in [1, 2, 3] : v * 2]
by_name = {for u in users : u.name => u.id}
```

**Templates — `${}` interpolation, `%{if}` / `%{for}` directives**

```hcl
message = "Hello, ${upper(name)}! %{if enabled}(on)%{else}(off)%{endif}"
```

A `~` glued to the inner side of `${`/`%{`/`}` trims the adjacent run of
whitespace (including newlines) from the literal text next to it — handy
for writing a `%{for}` across multiple source lines without a stray blank
line where the directives themselves sat:

```hcl
list_text = <<-EOF
  %{for v in items~}
  - ${v}
  %{endfor~}
  EOF
```

**Heredoc**

```hcl
script = <<-EOF
  #!/bin/bash
  echo "hi"
  EOF
```

**Function calls**

```hcl
full_name = concat(first_name, " ", last_name)
joined    = concat(list_a, list_b...) # "..." expands the last argument
```

## How traversal resolution works

Given `ROOT.step1.step2...`, evaluation tries, in order:

1. A local variable from an enclosing `for`-scope.
2. A top-level **attribute** literally named `ROOT` — its value becomes the
   base, and the remaining steps navigate into it.
3. Otherwise, a top-level **block** whose `type` is literally `ROOT`: as
   many leading steps as that block has **labels** are consumed to pick an
   instance (so `resource "type" "name" {}` is reached as
   `resource.type.name...`, two labels deep, not just one), and the rest of
   the steps navigate its body as an object. When several blocks share a
   type but declare different numbers of labels, the traversal that
   supplies enough matching steps for the longest label sequence wins.
4. If neither exists, evaluation fails with a "reference not found" error.

This is why `var = { name = "Ada" }` lets you write `var.name`, but
`var { name = "Ada" }` (an unlabeled *block* named `var`, not an attribute)
does **not** — there's no attribute called `var`, and the block-fallback
path needs `name` to be a block *label*, not an attribute key inside an
unlabeled block. Neither `var` nor any other name is special; this is just
the same generic algorithm applied to whatever identifier you used.

A cycle in that resolution (`a = b`, `b = a`, or a single `a = a`) is
caught and reported as an evaluation error instead of recursing until the
process crashes.

## Postfix chaining on any expression

`.attr`, `[index]`, and splat (`.*` / `[*]`) aren't limited to a bare
identifier root — they chain onto the result of a function call, a
parenthesized expression, an object/tuple literal, or a for-expression too:

```hcl
first_port  = servers()[0].port
picked      = (is_prod ? prod_config : dev_config).region
env_name    = {dev = "development", prod = "production"}["prod"]
doubled_one = [for v in [1, 2, 3] : v * 2][0]
```

Only a bare-identifier root (`a.b.c`) goes through the named-lookup
algorithm above; chaining onto anything else just evaluates that base
expression first and then applies the same steps to the resulting value.

## Dynamic indexing

`[...]` accepts any expression, not just a literal. The expression is
evaluated first (in the current scope, so `for` variables work), and the
kind of the resulting value decides what happens: a **number** indexes a
list (it must be a whole number within bounds), a **string** looks up an
object key, and anything else is an evaluation error.

```hcl
current_zone = zones[current]
size         = sizes[kind]
humidity     = modes[is_open ? "day" : "night"].humidity
last_zone    = zones[length(zones) - 1]
cell         = grid[row][col]
times        = [for z in zones : schedule[z]]
weight       = { small = 1, big = 2 }[kind]
```

A dynamic index never doubles as a block label: `plant.fern.sunlight`
reaches `plant "fern" {}`, but `plant[name].sunlight` does not, even when
`name` is `"fern"` — only `.label` steps take part in the block lookup
described above.

## External bindings

The host program can inject values into evaluation with a `cl_bindings_t`
and `cl_document_evaluate_with()`. The names are whatever the program
chooses; none of them is special to the library.

```hcl
# deploy.cl - "env", "replicas" and "build_id" come from the host program
service "api" {
  name     = "api-${env}"
  replicas = replicas
  image    = "registry.local/api:${build_id}"
}
```

```c
cl_bindings_t *b = cl_bindings_new();
cl_bindings_set_string(b, "env", "prod");
cl_bindings_set_number(b, "replicas", 6);
cl_bindings_set_string(b, "build_id", "a1b2c3");

cl_value_t *ips = cl_bindings_list(b);             /* lists and objects too */
cl_bindings_list_add(b, ips, cl_bindings_string(b, "10.0.0.1"));
cl_bindings_set(b, "ips", ips);

cl_evaluated_t *result = cl_document_evaluate_with(doc, b, &err);
cl_bindings_free(b);   /* safe: the result holds its own copies */
```

- **Lookup order**: `for` variables, then bindings, then top-level
  attributes, then labeled blocks.
- **Overriding defaults**: a binding replaces a same-named *top-level*
  attribute everywhere - where it is referenced and in the evaluated
  attribute itself. With `env = "dev"` in the file, evaluating without
  bindings yields `"dev"`; binding `env` to `"prod"` yields `"prod"`, and
  the file's own expression for `env` is not evaluated at all. Attributes
  nested inside blocks are never overridden.
- **Unbound names**: a document that references a name nobody defines
  fails with the usual "reference not found" error.
- **Ownership**: every `cl_value_t` built with `cl_bindings_*` belongs to
  that `cl_bindings_t`; adding a container into something it already
  contains is refused (`-1`), so bound values can't be cyclic.

`cl_tool` accepts bindings as `name=value` arguments (`true`/`false` become
bools, numeric text becomes a number, anything else a string):

```sh
./build/example/cl_tool deploy.cl env=prod replicas=6 build_id=a1b2c3
```

## Built-in functions

| Function            | Behavior                                                                 |
|----------------------|---------------------------------------------------------------------------|
| `upper(s)`           | Uppercases a string                                                       |
| `lower(s)`           | Lowercases a string                                                       |
| `length(x)`          | Element count of a list, or character count of a string                   |
| `concat(...)`        | Concatenates lists if every argument is a list; otherwise stringifies every argument and concatenates them as text |

Calling an unregistered function is an evaluation error, not a parse error.

## Known limitations

- The host can inject values but not functions: only the built-ins above
  are callable.
- Template trim markers (`~`) don't survive serialization as `~`: the
  writer emits the already-trimmed text, which reparses to the same result.

## Project layout

```
include/cl/cl.h   the entire public API
src/               implementation, one concern per file (lexer, parser,
                   evaluator, navigation, mutation, writer, ...)
example/           cl_example (dumps every cl/*.cl fixture) and
                   cl_tool <file.cl> [name=value ...] (dumps one file you pass in)
tests/             the CTest suite
cl/                .cl fixture files used by the examples and tests
```

## License

MIT — see [LICENSE](LICENSE).
