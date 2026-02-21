# 0000 - Header line syntax, 2026-01-12

## Issue

Herescript needs a syntax for header lines that follow the shebang. These lines specify arguments and environment bindings for the target executable. What prefix should identify these header lines?

## Decision

Header lines start with `#!` and the header block ends implicitly at the first line that does not start with `#!`.

## Rationale

The key principle is **opt-in semantics**: every header line must explicitly declare itself as herescript metadata by starting with `#!`. This prevents accidentally turning documentation comments into command-line arguments.

### Alternative Considered: `#` with explicit `###` terminator

Using plain `#` comments with an explicit `###` end marker was considered but rejected:

**Problems:**
- **Dangerous failure mode**: Forgetting `###` causes the entire script to be consumed as header lines
- **Ambiguity**: Cannot visually distinguish herescript metadata from ordinary comments
- **Accidental arguments**: A comment like `# TODO: fix this` would become an argument `TODO: fix this`

### Why `#!` works well

1. **Opt-in is safe by default**: You cannot accidentally create a header line - each one requires the explicit `#!` prefix

2. **Semantic coherence**: `#!` extends the shebang concept - the header block reads as "still part of the interpreter instructions"

3. **Self-terminating**: The header ends at any non-`#!` line (blank line, regular comment, or code) - no special end marker needed

4. **Rare in practice**: `#!` at start-of-line after the shebang is almost never seen in real scripts, minimising collision risk

5. **Cross-language compatibility**: Most languages that support shebangs will treat `#!` as a comment (or at worst, a syntax error on line 2+, which is acceptable since the script is meant to be run via herescript, not directly)
