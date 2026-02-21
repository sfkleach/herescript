# herescript Execution Specification

## 1. Overview

`herescript` executes a script according to metadata encoded in a header block
immediately following the shebang. It constructs:

- argv[] — argument vector
- envp[] — environment vector

and performs a single exec of the target executable.

Evaluation is deterministic, single‑pass, and non‑recursive.

---

## 2. Script Format

### 2.1 Shebang

The first line must be:

    #!/usr/bin/herescript <executable>

`<executable>` becomes argv[0]. 

NOTE: No option should be passed. If the first line has the form:

```
#!/usr/bin/herescript <executable> <option>
```

then herescript exits with non-zero status code. This is a deliberate decision to
create room for passing options to herescript rather than the executable in the
future.


### 2.2 Header Block

All immediately following lines beginning with:

    #!

are header lines. The header ends at the first non‑`#!` line.

---

## 3. Header Line Grammar

Normal header lines have the form:

    #!<metachars><whitespace><body>

Where:

- `<metachars>` are zero or more characters from: `=`, `$`, `\`, `!`, `#`, which modify the interpretation of <body>.
- `<whitespace>` is at least 1 space/tab character
- `<body>` is the remainder of the line after stripping leading and trailing whitespace, of which there are several types

Short header lines have an abbreviated form:

    #!<metachars>

In other words they lack any trailing whitespace. Because we don't want trailing
whitespace to be significant, we treat these as having empty bodies. It is
equivalent to them having a fictitious whitespace character at the end.

### 3.1 Comment line

A header line whose meta-characters includes a `#` is a comment line and discarded.

### 3.2 Environment variable binding

A header line may have a body that binds an environment variable. Typically it
will have the form `NAME=VALUE`. The exact regexp is:

```
([[:alpha:]_][[:alnum:]_]*)(:?=)(.*)
```

Notes:

- This interpretation of a header line is only allowed if the special interpretation of `=` is in force
  - Equivalent to not having `#`, `!` or `=` meta-characters.
- The first match group is the NAME, the second match group is the operator, and the third match group is the VALUE. 
- The NAME group is supposed to match the POSIX-compliant syntax of a shell variable.
- The operator may be `=` or `:=`, where the latter provides a default if it is not already set.


### 3.2 Argument

Almost all other header line's bodies are considered to be ordinary arguments.

There is an exception when the special interpretation of `=` is in force:
- **IF** the body contains an `=` character
- And the special interpretation is in force
- And the first character of the body is not a `-` (i.e. matches `[^-]`)
- **THEN** the header line is in error and herescript should exit with a non-zero status code.


### 3.3 Short header lines

- `#!` on its own is exceptionally interpreted as an ordinary argument that is an empty string. Not discarded.
- `#!` followed by any meta-character other than `#` means exactly the same. Not discarded.
- `#!` followed by meta-characters that include `#` means a comment. Discarded.

### 3.4 Examples

- `#!=    foo` → body is `foo`
- `#!   =foo` → body is `=foo`
- `#! # this is not a comment` → body is `# this is not a comment`
- `#!# this is a comment` → discarded

---



## 4. Meta‑character Semantics

Note that meta-characters may appear in any order, duplicates are ignored and overlaps (e.g. between `!` and `$`) are ignored.

### 4.1 `!` — literal mode

If `!` appears in `<metachars>`, **all special interpretation is disabled**:

- no escape processing
- no `${…}` substitution
- no binding detection
- `<body>` becomes a literal positional argument

`!` overrides all other meta‑characters.

### 4.2 `\` — disable escapes

If `\` appears in `<metachars>`, escape processing is disabled.

### 4.3 `$` — disable substitution

If `$` appears in `<metachars>`, `${…}` sequences are literal.

### 4.4 `=` — disable binding detection

If `=` appears in `<metachars>`, the line is treated as a positional argument even if it contains `=` or `:=`.


### 4.5 `#` - end of line comment

If `#` appears as a meta-character then the line is a comment and discarded.

---

## 5. Escape Processing

Performed first unless disabled by `\` or `!`.

Recognised escapes:

- `\\` → `\`
- `\n` → newline
- `\r` → carriage return
- `\t` → tab
- `\s` → space
- `\$` → `$`, disabling its special interpretation.

Unrecognised escapes become literal.


## 5.1 Escape Processing Interaction

Q: What does `#!\NAME=value` mean? 
A: It has no whitespace and hence it is an invalid header line.

Q: What about `#!= NAME=value` then?
A: The `=` metachar disables binding detection, so despite containing `=`, this is a positional argument with value `NAME=value`.

Q: What about `#! \NAME=value` then?
A: No metachars are present (space follows `#!` immediately). Escape processing applies but `\N` is unrecognised, so it remains literal. The result `\NAME=value` is not a valid binding (name starts with `\`), so it becomes a positional argument.



---

## 6. Substitution Processing

Performed after escapes unless disabled by `$` or `!`.

### 6.1 Environment substitution

`${NAME}` expands to the value of environment variable NAME.

Undefined NAME → **fatal error**.

### 6.2 Script name substitution

`${}` expands to the script filename.

### 6.3 Single‑pass rule

Substitution is **not recursive**. If expansion produces `${…}`, it remains literal.

---

## 7. Classification

After escapes and substitution. Note that NAME must be a valid non-empty,
environment variable name (POSIX standard). In order to gracefully handle long
arguments such as `--myoption=value`, the rule for recognising a line with `=`
in it is:

- The `=` metacharacter is enabled
- If the leading non-whitespace character is a `-` then `=` is disabled.
- If the name part of NAME=VALUE is not consistent with POSIX variable names, the parse will fail and an error is raised.

### 7.1 Conditional binding

    NAME:=VALUE

If NAME is unset, set it to VALUE. Otherwise leave unchanged.

### 7.2 Eager binding

    NAME=VALUE

Always sets NAME to VALUE. VALUE may be empty and this means set NAME to the
empty string. In addition VALUE may contain `=` characters. Only the first `=`
is given a special interpretation.

### 7.3 Positional argument

Any line not matching the above, or with `=` in `<metachars>`, becomes a positional argument.

---

## 8. Script Path Handling

By default, the script filename (fully expanded canonical path) is appended as the final positional argument.

If any argument contains `${}`, and it is expanded rather than just being a literal occurrence, this behaviour is disabled.

---

## 9. Execution

Construct:

- argv[0] = `<executable>`
- argv[1..N] = positional arguments
- optionally argv[N+1] = script filename

Construct the environment from the inherited environment plus bindings.

Execution follows the same semantics as `/usr/bin/env`:

- If `<executable>` contains no `/`, search `$PATH` for the executable.
- If `<executable>` contains `/`, use it directly as a path (absolute or relative).

Then replace the current process with the target executable, passing the
constructed argument vector and environment.

---

## 10. Error Conditions

herescript must fail if:

- `${NAME}` references an undefined variable
- the shebang is malformed
- header syntax is invalid
- the target executable cannot be invoked

A different status code should be assigned to each. Error messages should go to
stderr. Error messages should be human-friendly and explain the problem and hint
at the likely solution.

