# functests — Functional Tests for herescript

This folder contains the functional test suite for `herescript`. Tests are
defined in YAML files and executed by `functest.py`, which generates temporary
shell scripts, runs them, and compares the results against expected output.

## Folder Contents

| File / folder | Purpose |
|---|---|
| `functest.py` | The test runner. |
| `basic.yaml` | The primary test suite covering the full range of herescript features. |
| `pyproject.toml` | Python project metadata; managed with [uv](https://docs.astral.sh/uv/). |
| `Justfile` | Convenience recipes for running and checking the test suite. |

## Running the Tests

From the top-level project directory:

```sh
just functest            # type-check functest.py, then run all tests
```

Or run the suite directly with uv:

```sh
uv run --project functests python3 functests/functest.py
```

The runner discovers all `*.yaml` files in the same directory as `functest.py`
and executes every test case found in them.

**Prerequisites:** the `herescript` and `test-herescript` binaries must have
been built and placed in `_build/` before running the tests. From the project
root:

```sh
just build    # or: make
```

## Test File Format

Each YAML file has two top-level keys:

```yaml
build_dir: "${BUILD_DIR}"   # required — declares the build-dir token
tests:                      # required — list of test cases
  - name: my-test
    script: |
      ...
    output: |
      ...
```

The token `${BUILD_DIR}` is substituted with the absolute path to `_build/`
before any script is written or any output is compared. It may appear in
`script`, `output`, and `stderr` values.

### Test Case Fields

Each entry in the `tests` list supports the following fields.

#### `name` (required, string)

A short identifier for the test. It is used as the stem of the generated
script file written to `_build/<name>.sh`, so it must be unique within the
suite and should contain only characters that are safe in filenames.

#### `script` (required, string)

The full text of the shebang script to execute. The first line must be a valid
`#!` line invoking `herescript` from the build directory:

```yaml
script: |
  #!${BUILD_DIR}/herescript ${BUILD_DIR}/test-herescript
  #! --some-arg
```

The script is written to `_build/<name>.sh`, made executable, and then run
directly.

**Security note:** the runner enforces that the first line of every script
matches one of two permitted shebang patterns. Scripts that do not match are
blocked and counted as failures.

#### `output` (string, required unless `exit_code` is set)

The text that the script is expected to write to stdout. Trailing whitespace on
each side is ignored during comparison. `${BUILD_DIR}` tokens are expanded
before comparing.

Setting `output` to the literal string `...` (an ellipsis) disables stdout
comparison for that test. This is useful when only the exit code matters.

```yaml
output: |
  {
      "argv": ["${BUILD_DIR}/test-herescript", "hello"]
  }
```

#### `exit_code` (int or `nonzero`, default `0`)

The expected process exit code. Three forms are supported:

| Value | Meaning |
|---|---|
| `0` (default) | The script must exit successfully. |
| Any integer ≥ 1 | The script must exit with exactly that code. |
| `nonzero` | The script must exit with any non-zero code. Use this when the exact code varies across platforms (e.g. because the kernel parses the `#!` line differently on Linux vs macOS). |

```yaml
exit_code: nonzero
```

#### `stderr` (string, optional)

If present, the script's stderr output must match this value exactly (after
`${BUILD_DIR}` expansion and trailing-whitespace trimming). If absent, stderr
is not compared but is printed to the console on failure to aid debugging.

#### `env` (mapping, optional)

Environment variables to inject when the script is run. The script is executed
with a **clean** base environment (no inherited variables) extended by these
bindings. This ensures that variables inherited from the developer's shell do
not contaminate the JSON output that `test-herescript` prints.

```yaml
env:
  BASE_URL: https://example.com
  PORT: "8080"
```

### Minimal Examples

A test that checks stdout and expects success:

```yaml
- name: hello
  script: |
    #!${BUILD_DIR}/herescript ${BUILD_DIR}/test-herescript
    #! Hello, world!
  output: |
    {
        "env": [],
        "argv": ["${BUILD_DIR}/test-herescript", "Hello, world!", "${BUILD_DIR}/hello.sh"]
    }
```

A test that only checks the exit code, ignoring stdout:

```yaml
- name: bad-option
  script: |
    #!${BUILD_DIR}/herescript ${BUILD_DIR}/test-herescript --bad
    #! arg
  exit_code: nonzero
```

A test that injects environment variables and uses a conditional binding:

```yaml
- name: conditional
  env:
    PRE_SET: already-set
  script: |
    #!${BUILD_DIR}/herescript ${BUILD_DIR}/test-herescript
    #! PRE_SET:=should-not-override
    #! NEW_VAR:=default
  output: |
    {
        "env": ["PRE_SET=already-set", "NEW_VAR=default"],
        "argv": ["${BUILD_DIR}/test-herescript", "${BUILD_DIR}/conditional.sh"]
    }
```
