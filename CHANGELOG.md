# Change Log for Herescript

Following the style in https://keepachangelog.com/en/1.0.0/

## Unreleased

### Added

- In-file options via `#!` header lines:
  - `--chdir DIRECTORY` (or `--chdir=DIRECTORY`) — change the working directory before exec.
  - `--dry-run` — print planned `execve` arguments and environment without running.
  - `--load-file FILE` (or `--load-file=FILE`) — process `#:` lines from an external file before exec.
  - `--path-prepend DIRECTORY` (or `--path-prepend=DIRECTORY`) — prepend an
    existing directory (converted to an absolute path) to `PATH`.

## v0.1.1, 2026-04-18

## Added

- Added README.md the docs/tasks folder
- Adding unit test suite following JustCallMeRay's review

## Fixed

- Fix `herescript <exec>` (no script) crashing with NULL dereference on `argv[2]` by
  moving `--help` check before the argc guard and changing the guard from `argc < 2` to `argc < 3`
- Fix header block not terminating on unrecognised `#X` lines (e.g. `# comment` in the
  script body); `default: break` only exited the switch, not the header-parsing loop
- Fix inconsistent install path: README.md examples and `--help` text referenced
  `/usr/bin/herescript` but the Makefile installs to `/usr/local/bin/herescript`

## Changed

- Rename "Collaboration Style" section and clarify collaboration instructions in copilot-instructions.md


## v0.1.0

### Added

- Initial implementation of `herescript` as a single-file C program (`herescript.c`).
- Shebang-based launcher: scripts begin with `#!/usr/bin/herescript <executable>`,
  where `<executable>` is bound to `${HERESCRIPT_COMMAND}`.
- Three new header line types that cleanly replace the legacy `#!`-prefix approach:
  - `##` — comment line, discarded entirely.
  - `#:` — arguments and bindings line, parsed with shell-like tokenisation.
  - `#>` — inline quoted argument line; a bare `#>` or one not followed by a
    space is a syntax error.
- Shell-like tokenisation on `#:` lines, supporting:
  - Runs of whitespace as token separators.
  - `'...'` — plain single-quoted literals (no escapes, no substitution).
  - `$'...'` — escape-quoted single-quoted strings; supports `\\`, `\'`, `\"`,
    `\n`, `\r`, `\t`, `\s`.
  - `"..."` — double-quoted strings with variable interpolation and minimal
    backslash escapes (`\\`, `\"`, `\$`).
  - `$"..."` — escape-quoted double-quoted strings (full backslash escape
    expansion as per `$'...'`, plus variable interpolation).
- Variable and parameter substitution inside `#:` lines and double-quoted spans:
  - `${NAME}` — expands to the value of the named environment variable; exits
    with an error if undefined.
  - `${N}` — expands to the N-th user-supplied argument (`${0}` is the
    canonicalised script path, synonymous with `${HERESCRIPT_FILE}`).
  - `${A:B}` — parameter slice; expands to the arguments from index A up to
    (but not including) B, each as a separate token. A and B default to 0 and
    argc respectively when omitted. `$@` is a synonym for `${1:}`.
  - `${:}` — expands to `${HERESCRIPT_FILE}` followed by all user-supplied
    arguments; provided as a convenient default invocation.
  - `${HERESCRIPT_FILE}` — the canonicalised path of the running script.
  - `${HERESCRIPT_COMMAND}` — the executable named on the `#!` line.
  - `${NAME-VALUE}` — expands to `VALUE` when `NAME` is unset; otherwise
    expands to the current value of `NAME`.
  - `${NAME:=VALUE}` — equivalent to `${NAME-VALUE}`; mirrors the `#:` binding
    syntax to reduce cognitive load.
  - `$NAME` — bare unbraced identifier; greedily consumes `[a-zA-Z_][a-zA-Z0-9_]*`.
  - `$N…` — bare unbraced digit run; greedily consumes all consecutive digits,
    so `$10` expands parameter 10.
- Environment variable bindings on `#:` lines:
  - `NAME=VALUE` — unconditionally sets `NAME` in the child environment.
  - `NAME:=VALUE` — sets `NAME` only when it is not already defined in the
    environment (conditional initialisation).
  - Bindings are recognised only as leading tokens; quoting the name or
    separator suppresses binding recognition and treats the token as a plain
    argument.
- Inline quoted arguments via `#>` lines:
  - A run of consecutive `#>` lines is glued together (preserving embedded
    newlines) and bound to `${HERESCRIPT0}`, `${HERESCRIPT1}`, etc.
  - A `#:` line between two `#>` runs forces a break, starting a new argument.
- Executable lookup via `PATH` when the shebang does not specify an absolute path.
