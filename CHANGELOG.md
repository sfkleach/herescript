# Change Log for Herescript

Following the style in https://keepachangelog.com/en/1.0.0/

## v0.1.0

### Added

- Initial implementation of `herescript` as a single-file C program (`herescript.c`).
- Shebang-based launcher: scripts begin with `#!/usr/bin/herescript <executable>`.
- Multi-line header block parsing — all lines following the shebang that begin
  with `#!` are treated as herescript metadata.
- Positional argument declarations: header lines contribute arguments passed to
  the target executable.
- Environment variable bindings:
  - `NAME=VALUE` unconditionally sets `NAME` in the child environment.
  - `NAME:=VALUE` sets `NAME` only if it is not already defined (conditional
    initialisation).
- `${NAME}` substitution — expands to the value of the named environment
  variable; exits with an error if the variable is undefined.
- `${}` substitution — expands to the canonical path of the script, and
  suppresses the automatic appending of the script path that would otherwise
  occur.
- Escape sequences in header bodies: `\\` → `\`, `\n` → newline, `\r` →
  carriage return, `\t` → tab, `\s` → space, `\$` → literal `$`.
- Per-line metacharacter flags to selectively disable processing:
  - `!` — literal mode, disables all special processing.
  - `\` — disables escape processing.
  - `$` — disables `${…}` substitution.
  - `=` — disables environment binding detection.
  - `#` — marks the line as a comment (discarded entirely).
- Executable lookup via `PATH` when the shebang does not specify an absolute
  path.
- Script path canonicalisation via `realpath()` before execution.
- Deterministic process replacement using `execve()`.
- `--help` option: when invoked with `--help` as the sole argument, prints a
  concise usage summary and exits.

