# 0002 - Extend single-file capability, 2026-04-25

## Issue

A herescript script is a single shebang file. A core design goal is to
maximise what that file can express, so that authors are not forced to
front a script with a wrapper just to handle variation between deployment
environments (developer laptop, CI runner, production host, and so on).

When an option's behaviour depends on whether a resource exists — for example,
whether an environment variable is set — a single hard-coded policy (always
error, or always silent) cannot serve all environments. A strict default would
force authors to write a wrapper for any "variable may or may not be set" case;
a universally lenient default would silently mask mistakes.

## Decision

Options that operate on potentially-absent resources should offer a companion
modifier that the author can declare in the `#!` header. The modifier defaults
to `error` (strict), but the author can opt into `warning` or `allow` within
the single file itself.

For example, the planned `--unset-undefined=(error|warning|allow)` modifier
governs `--unset`: by default the script errors if the named variable is not
set, but writing `#! --unset-undefined=allow` ahead of the `--unset` directives
expresses the lenient intent explicitly and keeps the script self-contained.

## Rationale

The key principle is: **all deployment-environment variation should be
expressible within the single shebang file**.

If an option's only available behaviour is strict, any script that needs to run
in environments where the resource may be absent requires a wrapper. The wrapper
defeats the single-file goal and adds distribution and audit burden.

Providing a modifier option with a strict default gives the best of both worlds:

- Scripts that don't use the modifier behave safely and explicitly.
- Scripts that need lenient semantics declare that intent in the header rather
  than relying on silent no-ops, keeping the behaviour visible and auditable.

This also addresses security-review concerns about "fail-open" behaviour: any
leniency is a deliberate, visible declaration by the author, not a hidden
default.
