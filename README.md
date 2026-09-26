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

This runs the `cl_tests` suite (in `tests/`) as 7 named CTest cases:
`parser`, `eval`, `navigate`, `writer`, `fixtures`, `bindings`, and `schema`.

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

The entire public API lives in `include/cl/cl.h`. See `example/tool.c` (the
`cl_tool` program) for a fuller example that dumps both the raw AST and the evaluated tree of
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

**Every quoted string is a template**

The same rule applies wherever a quoted string appears: attribute values,
`["..."]` indexes, object keys and block labels. `${...}` always
interpolates, and `$${...}` / `%%{...}` are always the literal text
`${...}` / `%{...}`.

```hcl
env = "prod"

hosts = {
  "db_${env}"  = "10.0.0.5"     # key "db_prod"
  "$${raw}"    = "literal"      # key "${raw}", literally
  (upper(env)) = "computed"     # "(expr)" keys too: key "PROD"
}
db = hosts["db_${env}"]         # "10.0.0.5"

server "web-${env}" {           # label "web-prod"
  port = 80
}
port = server.web-prod.port     # 80
```

- **Object keys**: a bare identifier is always a literal key (`{ env = 1 }`
  has the key `env`, not the value of `env`). A computed key may be a
  string, a number or a bool, and numbers and bools become their text.
  A key that appears twice in the same object, whether written
  literally or computed, is an evaluation error.
- **Computed block labels are a cl extension**: HCL only accepts literal
  labels. A computed label is evaluated at the top level, so it can use
  attributes, other blocks and bindings. A label that needs its own
  block to get its value (for example `name = server.api.port` with
  `server "${name}" {}`) is reported as a circular reference. Before
  evaluation, a computed label has no value: `cl_block_t.labels[i]` is
  `NULL`, its template is in `label_exprs[i]`, and the AST lookup
  `cl_body_find_block()` never matches it. Computed object keys work the
  same way (`key` is `NULL` and `key_expr` holds the expression).

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
./build/bin/cl_tool deploy.cl env=prod replicas=6 build_id=a1b2c3
```

## Host functions

The same `cl_bindings_t` can register C callbacks that the document calls
like built-ins:

```c
/* slug("Hello World") -> "hello-world" */
static cl_value_t *slug(cl_call_t *call, void *userdata) {
    const char *s = cl_value_as_string(cl_call_arg(call, 0));
    if (!s) {
        return cl_call_error(call, "argument 1 must be a string");
    }
    char buf[256];
    size_t n = 0;
    for (; s[n] && n + 1 < sizeof(buf); n++) {
        buf[n] = s[n] == ' ' ? '-' : (char)tolower((unsigned char)s[n]);
    }
    buf[n] = '\0';
    return cl_call_string(call, buf);
}

cl_bindings_set_function(b, "slug", 1, 1, slug, NULL);  /* name, min, max args */
cl_bindings_set_function(b, "sum", 0, CL_FUNCTION_VARIADIC, sum, NULL);
```

```hcl
path = "/posts/${slug(title)}"
```

- **Arity**: the callback only runs when the argument count is within
  `[min_args, max_args]`; otherwise evaluation fails with
  `slug() expects 1 argument, got 2`. Arguments arrive already evaluated,
  with a trailing `...` already expanded.
- **Arguments**: `cl_call_argc()` / `cl_call_arg()`, read with the usual
  `cl_value_*` getters. They are shared with the rest of the document, so
  never modify them.
- **Results**: build them with `cl_call_string/number/bool/null/list/object`
  and `cl_call_list_add` / `cl_call_object_set` (which only accept
  containers created by the same call, and refuse cycles), or return an
  argument as-is. Anything else, such as a value built with
  `cl_bindings_*`, goes through `cl_call_copy()` first.
- **Errors**: `return cl_call_error(call, "fmt", ...)` fails the evaluation
  with `slug(): <message>` at the call's position. Returning `NULL` without
  it fails with a generic message.
- **Precedence**: a host function shadows a built-in of the same name.
  Functions and value bindings live in separate namespaces, so `x` can be
  both.
- **Lifetime**: `userdata` is passed back to every call and must stay valid
  while evaluating. Callbacks only run inside `cl_document_evaluate_with()`,
  so the bindings can still be freed as soon as it returns.

## Block schemas

The language never decides whether a block type "exists" or which
arguments it may hold. A **schema** lets the host program decide that for
the block types it cares about, and have `cl` check documents against it.
Validation is a separate, explicit step: loading and evaluating work
exactly as before.

A schema can be written as a `.cl` file (see
`cl/schema/machine.schema.cl`):

```hcl
strict = true                     # optional: unregistered top-level block types are errors

block "machine" {
  cpu    = { type = "number", required = true }
  memory = { type = "string", required = true }
  tags   = "list"                 # short form: just the type, optional

  block "disk" {                  # sub-block allowed inside "machine"
    size = { type = "number", required = true }
    kind = ["ssd", "hdd", "nvme"]   # enum: one of these strings, optional
  }
}
```

or built in C:

```c
cl_schema_t *schema = cl_schema_new();
cl_schema_block_t *machine = cl_schema_add_block(schema, "machine");
cl_schema_block_add_attr(machine, "cpu", CL_TYPE_NUMBER, 1);    /* required */
cl_schema_block_add_attr(machine, "tags", CL_TYPE_LIST, 0);     /* optional */
cl_schema_block_t *disk = cl_schema_block_add_block(machine, "disk");
cl_schema_block_add_attr(disk, "size", CL_TYPE_NUMBER, 1);
static const char *kinds[] = {"ssd", "hdd", "nvme"};
cl_schema_block_add_enum(disk, "kind", kinds, 3, 0);            /* enum */
```

Then validate. Pass the document alone to check structure (and the types
of literal values), and pass the evaluated result as well to check the
type of every evaluated value:

```c
if (cl_schema_validate(schema, doc, NULL, &err) != 0) { /* ... */ }
cl_evaluated_t *result = cl_document_evaluate(doc, &err);
if (cl_schema_validate(schema, doc, result, &err) != 0) { /* ... */ }
```

Rules:

- Rules match **top-level blocks by type**. Labels are not checked, so
  `machine "web" {}` and `machine {}` use the same rule.
- Inside a registered block everything is closed. Only declared attributes
  may appear, each at most once, and required ones must be present. Only
  declared sub-blocks may appear, and each is checked with its own rules.
- Types are `any`, `string`, `number`, `bool`, `list` and `object`. Only
  `any` accepts `null`.
- An **enum** is a string attribute restricted to a fixed set of values.
  It is written as a list of strings (`kind = ["ssd", "hdd"]`), or as
  `{ type = "string", required = true, values = [...] }` when it is
  required. Matching is exact and case-sensitive. Enums hold strings only.
- Without a result, a type or enum value is only checked when the value is a literal. An
  expression like `cpu = base * 2` is checked once you pass the result,
  and so are values injected through bindings.
- Top-level blocks of an unregistered type are ignored, unless the schema
  is `strict`. Top-level attributes are never checked.
- Validation stops at the first problem and reports its line and column,
  for example `attribute 'gpu' not allowed in block 'machine'`.
- `block` and `strict` belong to the schema file format only. Ordinary
  documents still have no special names.

`cl_tool` validates when given a schema file:

```sh
./build/bin/cl_tool cl/schema/machine.cl --schema=cl/schema/machine.schema.cl
```

## Reading values

After evaluation, the `cl_get_*` functions read a value by **path**
instead of walking `cl_evaluated_body_t` / `cl_value_t` by hand. They only
work on the evaluated result; the raw AST keeps its own navigation API.

```hcl
# app.cl
port = 8080

server "web" {
  host = "web.local"
  tls {
    enabled = true
  }
}

machine "m1" {
  disk {
    size = 100
  }
  disk {
    size = 200
  }
}
```

```c
const cl_evaluated_body_t *root = cl_evaluated_root(ev);

long port    = cl_get_int(root, "port", 80);                       /* 8080 */
int  tls     = cl_get_bool(root, "server.web.tls.enabled", 0);     /* 1 */
long workers = cl_get_int(root, "workers", 4);                     /* 4: not defined */

const cl_evaluated_block_t *web = cl_get_block(root, "server.web");
const char *host = web ? cl_get_string(web->body, "host", "localhost") : "localhost";

cl_block_iter_t it;
const cl_evaluated_block_t *disk;
long total = 0;
cl_block_iter_init(&it, root, "machine.m1.disk");
while ((disk = cl_block_iter_next(&it)) != NULL) {
    total += cl_get_int(disk->body, "size", 0);                    /* 100 + 200 */
}
```

| Function                                  | Returns                                           |
|-------------------------------------------|---------------------------------------------------|
| `cl_get_string(base, path, def)`          | the string, or `def` (which may be `NULL`)        |
| `cl_get_int(base, path, def)`             | the number as a `long`, or `def`                  |
| `cl_get_number(base, path, def)`          | the number as a `double`, or `def`                |
| `cl_get_bool(base, path, def)`            | the bool, or `def`                                |
| `cl_get_list(base, path)`                 | the list value, or `NULL`                         |
| `cl_get_value(base, path)`                | a value of any kind (objects included), or `NULL` |
| `cl_get_block(base, path)`                | the block the path names, or `NULL`               |
| `cl_block_iter_init` / `cl_block_iter_next` | every block of one type, in document order     |

- **Base**: `cl_evaluated_root(ev)` for the whole document, or a block's
  `->body` to read relative to that block.
- **Paths**: `server.web.port`, `disks[0].size`, `tags["Name"]`, and
  `["a.b"]` for names holding `.` or `[` (quoted names take no escapes).
  In a body, an **attribute** wins; otherwise a **block** of that type is
  picked by the next parts as its labels, with the same longest-match rule
  as [traversal resolution](#how-traversal-resolution-works), at every
  level of the path. Unlike traversals, this also reaches unlabeled blocks
  (`server.port` reads `server { port = 82 }`).
- **Defaults**: a missing path, a malformed path, a `null` value and a
  value of the wrong type all give `def` (or `NULL`). `cl_get_int` also
  gives `def` for a number with a fractional part or outside `long`.
  Use `cl_get_value()` to tell "not defined" apart from a default.
- **Iterator**: the last part of the path is the block type; the parts
  before it name the block to search in (`"disk"` alone searches `base`).
  It lives on the stack and allocates nothing, but keeps a pointer into
  `path`, which must outlive it.

### More helpers

```hcl
mode  = "safe"
ports = [80, 443]

server "web" {
  port = 8080
}

service "api" {
  env = { LOG = "debug", REGION = "sa-east-1" }
}
```

```c
static const char *const modes[] = {"fast", "safe", "debug"};
int mode = cl_get_enum(root, "mode", modes, 3, 0);                /* 1 */

long ports[8];
size_t n = cl_get_ints(root, "ports", ports, 8);                   /* 2: 80, 443 */

long port = cl_get_intf(root, 80, "server.%s.port", name);         /* 8080 when name is "web" */

if (cl_get_kind(root, "server.web") == CL_GET_BLOCK) { /* ... */ }
size_t servers = cl_get_count(root, "server");                     /* 1 */

cl_attr_iter_t it;
const char *key;
const cl_value_t *value;
cl_attr_iter_init(&it, root, "service.api.env");
while (cl_attr_iter_next(&it, &key, &value)) {
    printf("%s=%s\n", key, cl_value_as_string(value));             /* LOG=debug, REGION=sa-east-1 */
}
```

| Function                                         | Does                                                                 |
|--------------------------------------------------|----------------------------------------------------------------------|
| `cl_get_kind(base, path)`                        | what the path names: `CL_GET_STRING` … `CL_GET_BLOCK`, or `CL_GET_MISSING` (null included) |
| `cl_has(base, path)`                             | 1 when the path names a block or a non-null value                    |
| `cl_get_count(base, path)`                       | items of a list, keys of an object, or the blocks `cl_block_iter` would walk |
| `cl_get_enum(base, path, names, count, def)`     | index of the string in `names` (exact match), or `def`               |
| `cl_get_strings` / `cl_get_ints` / `cl_get_numbers` `(base, path, out, max)` | copy a list into a C array; see below |
| `cl_attr_iter_init` / `cl_attr_iter_next`        | every attribute of a block body (nested blocks skipped) or every key of an object; `NULL`/`""` walks `base` |
| `cl_get_stringf` / `cl_get_intf` / `cl_get_boolf` / `cl_get_blockf` | the same getters with a `printf`-style path              |

- **List copies**: when every item has the right type (for
  `cl_get_ints`, integral numbers that fit in a `long`), the first
  `min(count, max)` items are written and the list's full count is
  returned, so a result larger than `max` means `out` was too small. Pass
  `NULL, 0` to only ask for the count. Otherwise the result is 0 and `out`
  is left untouched. Copied strings still belong to the evaluated result.
- **Formatted paths**: the formatted text is an ordinary path, so a label
  holding `.` or `[` still needs the `["..."]` form. `def` comes before the
  format string because of the variadic arguments.

## Exporting

An evaluated result - or a single value or block of it - can be written
back out as text, in cl syntax or as JSON. Each function returns a
`malloc`'d string that you release with `free()`.

| Function                              | Writes                                               |
|---------------------------------------|------------------------------------------------------|
| `cl_value_to_string(value)`           | one value in cl syntax: `"a"`, `42`, `[1, 2]`, `{ k = "v" }` |
| `cl_value_to_json(value)`             | one value as JSON                                    |
| `cl_evaluated_to_string(result)`      | the whole document, flattened (see below)            |
| `cl_evaluated_to_json(result)`        | the whole document as JSON                           |
| `cl_evaluated_block_to_string(block)` | one block (as returned by `cl_get_block`)            |
| `cl_evaluated_block_to_json(block)`   | one block as JSON                                    |

- **Flattened cl documents**: every expression, template and binding is
  already resolved, so the output is plain literals. Loading and
  evaluating it gives the same values again, which makes it a handy way to
  see the final configuration.
- **JSON layout**: nothing is merged or renamed, so every document can be
  represented. A body is `{"attributes": {...}, "blocks": [...]}` and a
  block is `{"type": ..., "labels": [...], "body": <body>}`, with blocks in
  document order:

  ```json
  {
    "attributes": {
      "port": 8080
    },
    "blocks": [
      {
        "type": "server",
        "labels": ["web"],
        "body": {
          "attributes": {
            "host": "web.local"
          },
          "blocks": []
        }
      }
    ]
  }
  ```
- **Details**: numbers keep full precision (integers without a fraction,
  otherwise the shortest form that reads back exactly). A repeated
  attribute name keeps only its first value in JSON, which is the value
  every lookup sees, so keys stay unique. NaN and infinities become `null`
  in JSON.

`cl_tool` exposes both. With `--json` or `--get`, it prints only the
result, so the output can be piped into other tools. Errors go to stderr
with exit status 1.

```sh
./build/bin/cl_tool app.cl --json                   # whole document as JSON
./build/bin/cl_tool app.cl --get=server.web.port    # 80
./build/bin/cl_tool app.cl --get=server.web         # the block, in cl syntax
./build/bin/cl_tool app.cl --get=server.web --json  # the block as JSON
```

## Built-in functions

The host can add its own functions, or replace these, through
[host functions](#host-functions). Calling an unknown function, or a
function with the wrong number of arguments, is an evaluation error, not a parse error. Where a function
takes a string, numbers and bools are accepted and converted the same way
template interpolation does (`upper(1.5)` is `"1.5"`). String positions
and lengths count bytes, not characters. A trailing `...` expands a list
into arguments: `max(ports...)`.

**Strings**

| Function                     | Behavior                                                                 |
|------------------------------|---------------------------------------------------------------------------|
| `upper(s)` / `lower(s)`      | Uppercases / lowercases a string                                          |
| `length(x)`                  | Byte count of a string, item count of a list or object                    |
| `concat(...)`                | Concatenates lists if every argument is a list; otherwise stringifies every argument and concatenates them as text |
| `trim(s, cutset)`            | Removes leading and trailing characters found in `cutset`                 |
| `trimspace(s)`               | Removes leading and trailing whitespace                                   |
| `trimprefix(s, prefix)`      | Removes `prefix` if `s` starts with it                                    |
| `trimsuffix(s, suffix)`      | Removes `suffix` if `s` ends with it                                      |
| `replace(s, search, repl)`   | Replaces every literal occurrence of `search`                             |
| `split(sep, s)`              | Splits into a list of strings (`split(",", "")` is `[""]`)                |
| `join(sep, list)`            | Joins list items (strings, numbers, bools) with `sep`                     |
| `substr(s, offset, length)`  | Substring; negative `offset` counts from the end, `length` -1 means "to the end" |
| `startswith(s, prefix)` / `endswith(s, suffix)` / `strcontains(s, sub)` | Bool tests |
| `format(fmt, args...)`       | `%s` (any string-like value), `%d` (integer), `%f` / `%.2f` (number), `%%` |

**Numbers**

| Function                     | Behavior                                                                 |
|------------------------------|---------------------------------------------------------------------------|
| `min(n...)` / `max(n...)`    | Smallest / largest argument                                               |
| `abs(n)`                     | Absolute value                                                            |
| `floor(n)` / `ceil(n)`       | Rounds down / up                                                          |
| `round(n)`                   | Rounds to nearest, halves away from zero                                  |
| `pow(base, exp)`             | `base` raised to `exp` (error when not a real number)                     |
| `parseint(s, base)`          | Parses an integer string in base 2–36                                     |

**Collections**

| Function                     | Behavior                                                                 |
|------------------------------|---------------------------------------------------------------------------|
| `keys(obj)` / `values(obj)`  | Keys / values as a list, in declaration order                             |
| `lookup(obj, key[, default])`| Value at `key`; `default` if missing (error if missing and no default)    |
| `merge(obj...)`              | Merges objects; later ones win, keys keep their first position            |
| `contains(list, value)`      | Whether `list` holds a value equal (`==`) to `value`                      |
| `element(list, index)`       | Item at `index`, wrapping around past the end                             |
| `slice(list, start, end)`    | Items from `start` (inclusive) to `end` (exclusive)                       |
| `reverse(list)`              | Items in reverse order                                                    |
| `distinct(list)`             | Removes duplicates, keeping first occurrences                             |
| `flatten(list)`              | Flattens nested lists at any depth                                        |
| `range([start,] end[, step])`| Numbers from `start` (default 0) up to, not including, `end`; step defaults to 1, or -1 when `start > end`; at most 1048576 items |
| `zipmap(keys, values)`       | Builds an object from two lists of the same length                        |

**Types and conversion**

| Function                     | Behavior                                                                 |
|------------------------------|---------------------------------------------------------------------------|
| `type(x)`                    | `"string"`, `"number"`, `"bool"`, `"null"`, `"list"` or `"object"`        |
| `tostring(x)`                | String form of a string, number or bool                                   |
| `tonumber(x)`                | Number from a number or a numeric string                                  |
| `tobool(x)`                  | Bool from a bool or `"true"` / `"false"`                                  |
| `coalesce(x...)`             | First non-null argument                                                   |

The `to*()` conversions return `null` unchanged.

## Known limitations

- Template trim markers (`~`) don't survive serialization as `~`: the
  writer emits the already-trimmed text, which reparses to the same result.
- NaN has no literal form: `cl_value_to_string()` and the document export
  write it as `null` (infinities are fine: `1e999` / `-1e999`).

## Project layout

```
include/cl/cl.h   the entire public API
src/               implementation, one concern per file (lexer, parser,
                   evaluator, navigation, mutation, writer, ...)
example/           cl_example (dumps every cl/*.cl fixture) and
                   cl_tool <file.cl> [--schema=<schema.cl>] [--json]
                   [--get=<path>] [name=value ...] (dumps or exports
                   one file you pass in)
tests/             the CTest suite
cl/                .cl fixture files used by the examples and tests
                   (cl/schema/: the block schema example)
```

## License

MIT — see [LICENSE](LICENSE).
