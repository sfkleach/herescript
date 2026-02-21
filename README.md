# herescript

[![Build and Test](https://github.com/sfkleach/herescript/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/sfkleach/herescript/actions/workflows/build-and-test.yml)

`herescript` is a modern, structured interpreter launcher designed to extend the limited Unix shebang mechanism. It provides:

- multi‑line argument declarations
- environment bindings
- conditional environment initialisation
- strict, single‑pass substitution
- explicit escape semantics
- deterministic execution via `execve()`

Scripts begin with:

    #!/usr/bin/herescript <executable>

Followed by a header block of lines beginning with `#!`.

## Syntax

### Header Line Syntax

Header lines follow the shebang and have the form:

    #!<metachars><whitespace><body>

Where:

- `<metachars>` are zero or more characters from: `=`, `$`, `\`, `!`, `#`
- `<whitespace>` is at least one space or tab
- `<body>` is the remainder after stripping leading/trailing whitespace

### Meta-characters

| Char | Effect |
|------|--------|
| `!`  | Literal mode — disables all special processing |
| `\`  | Disable escape processing |
| `$`  | Disable `${…}` substitution |
| `=`  | Disable environment binding detection |
| `#`  | Comment line (discarded) |

### Escape Sequences

When enabled: `\\` → `\`, `\n` → newline, `\r` → carriage return, `\t` → tab, `\s` → space, `\$` → literal `$`.

### Substitution

- `${NAME}` expands to the value of environment variable NAME (error if undefined)
- `${}` expands to the script filename

### Environment Bindings

- `NAME=VALUE` — always sets NAME to VALUE
- `NAME:=VALUE` — sets NAME only if currently unset

Arguments starting with `-` (e.g. `--option=value`) are treated as positional arguments, not bindings.

### Script Path Handling

By default, the script's canonical path is appended as the final argument. Using `${}` anywhere disables this automatic appending.
