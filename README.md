# herescript

[![Build and Test](https://github.com/sfkleach/herescript/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/sfkleach/herescript/actions/workflows/build-and-test.yml)

## What is herescript for?

I have always loved the conceptual elegance of Unix's `#!` mechanism for
treating scripts as executables. But it is limited to passing only a single
option (on Linux), and the position of the script file in the argument list is
fixed.

The idiomatic use of `#!/usr/bin/env <program>` simply to locate the program on
`$PATH` hints at common, unmet requirements. `herescript` is a tiny launcher
that fixes these limits: it performs path expansion, supports shell-like
argument construction, allows binding environment variables, and more — all in
a single, readable script without any additional files.

## Overview

`herescript` is a modern, structured interpreter launcher designed to extend the limited Unix shebang mechanism. It provides:

- shell-like argument declarations with full quoting and substitution
- environment bindings, both unconditional and conditional
- inline defaults at the point of use
- multi-line inline quoted arguments
- deterministic execution via `execve()`

Scripts begin with a standard Unix shebang, followed by a header block of
structured comment lines:

```
#!/usr/local/bin/herescript python3
## Run with verbose output and a fixed library path.
#: PYTHONPATH=/usr/local/lib
#: LOG_LEVEL:="info"
#: --verbose --output=/tmp/out ${:}
```

## Header Lines

The header block consists of all lines immediately following the `#!` shebang
that begin with `##`, `#:`, or `#>`. The first non-header line ends the block.

| Prefix | Meaning |
|--------|---------|
| `##`   | Comment — discarded entirely. |
| `#:`   | Arguments / bindings line — parsed with shell-like tokenisation. |
| `#> `  | Inline quoted argument — must be followed by exactly one space. |

## Shell-like Tokenisation (`#:` lines)

Tokens on `#:` lines are separated by whitespace. The following quoting and
substitution forms are supported:

| Form     | Description |
|----------|-------------|
| `'...'`  | Plain single-quoted literal. No escapes, no substitution. |
| `$'...'` | Escape-quoted string. Supports `\\` `\'` `\"` `\n` `\r` `\t` `\s`. |
| `"..."`  | Double-quoted string. Variable interpolation; `\\` `\"` `\$` escapes. |
| `$"..."` | Escape-quoted double-quoted string. Full backslash escapes plus interpolation. |

## Substitution Forms

The following substitutions are available on `#:` lines and inside `"..."`:

| Form                    | Expands to |
|-------------------------|-----------|
| `${NAME}`               | Value of environment variable `NAME`; error if undefined. |
| `$NAME`                 | Same as `${NAME}`, using greedy identifier matching. |
| `${N}`                  | N-th user-supplied argument. `${0}` is the canonicalised script path. |
| `$N…`                   | Same as `${N}`, consuming all consecutive digits (so `$10` is parameter 10). |
| `${A:B}`                | Parameter slice: arguments A up to (not including) B, each as a separate token. A and B default to 0 and argc when omitted. |
| `$@`                    | Synonym for `${1:}` — all user-supplied arguments. |
| `${:}`                  | `${HERESCRIPT_FILE}` followed by all user-supplied arguments; the typical default invocation. |
| `${HERESCRIPT_FILE}`    | Canonicalised path of the running script. |
| `${HERESCRIPT_COMMAND}` | The executable named on the `#!` line. |
| `${NAME-VALUE}`         | `VALUE` when `NAME` is unset; otherwise the current value of `NAME`. |
| `${NAME:=VALUE}`        | Same as `${NAME-VALUE}`; mirrors the binding syntax. |

## Environment Bindings (`#:` lines)

A `#:` line whose first token(s) match `IDENTIFIER=...` or `IDENTIFIER:=...`
are treated as environment variable bindings rather than arguments.

```
#: NAME=VALUE        # Always sets NAME.
#: NAME:=VALUE       # Sets NAME only if it is not already in the environment.
```

Bindings are recognised only as leading tokens on a line. Once a non-binding
token is seen, all remaining tokens are plain arguments. Quoting the identifier
or separator prevents binding recognition.

## Inline Quoted Arguments (`#>` lines)

A run of consecutive `#>` lines is collected into a single multi-line string
(line breaks preserved) and bound to `${HERESCRIPT0}`, `${HERESCRIPT1}`, etc.
A `#:` line between two `#>` runs forces a break, starting a new argument.

```
#!/usr/local/bin/herescript python3
#> first line of argument 0
#> second line of argument 0
#:
#> argument 1 starts here
```

A `#>` not followed by exactly one space is a syntax error.

## Special Environment Variables

| Variable                        | Value |
|---------------------------------|-------|
| `HERESCRIPT_FILE`               | Canonicalised path of the running script. |
| `HERESCRIPT_COMMAND`            | The executable from the `#!` line. |
| `HERESCRIPT0`, `HERESCRIPT1`, … | Inline quoted arguments from `#>` lines. |

## Installation

### From prebuilt binaries

Pre-built binaries for Linux and macOS are available on the
[releases page](https://github.com/sfkleach/herescript/releases). The
easiest way to install is with the bundled installer script:

```
curl -fsSL https://raw.githubusercontent.com/sfkleach/herescript/main/install.sh | sh
```

This detects your OS, downloads the correct binary from the latest stable
release, and installs it to `~/.local/bin`. This directory is on `$PATH` in
most modern distributions, but it is worth checking if `herescript` is not
found after installation.

To install a specific version (including pre-releases), set `VERSION`:
```
curl -fsSL https://raw.githubusercontent.com/sfkleach/herescript/main/install.sh | VERSION=v0.1.0-rc1 sh
```

To install elsewhere, set `INSTALL_DIR`. Note that you will need write
permission to the target directory:
```
curl -fsSL https://raw.githubusercontent.com/sfkleach/herescript/main/install.sh | INSTALL_DIR=/usr/local/bin sh
```

Alternatively, download the binary directly from the
[releases page](https://github.com/sfkleach/herescript/releases) and
place it on your `PATH` manually.


### From source

Build from source with `make` (requires a C11 compiler):

```
make
sudo make install
```

## Usage

```
#!/usr/local/bin/herescript <executable> [options]
```

When invoked directly:

```
herescript --help
```

