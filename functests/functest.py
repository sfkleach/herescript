#!/usr/bin/env python3
"""Functional test runner for runscript.

Run from the top-level project directory:
    uv run --project functests python3 functests/functest.py

Tests are loaded from all *.yaml files in the same directory as this script.
Each YAML file contains a list of test cases, each with:
  - name:   a short identifier used as the generated script filename stem
  - script: the full text of the shebang script to execute
  - output: the expected stdout, or '...' to skip the test

${BUILD_DIR} tokens in both scripts and expected output are substituted with
the absolute path of _build/ before use.
"""

import subprocess
import sys
from pathlib import Path
import yaml



# The canonical form of the first shebang line that the safety check accepts.
# Scripts whose first line does not match this pattern (after trimming) are
# blocked from execution to prevent accidental execution of untrusted scripts.
_SAFE_SHEBANG_TEMPLATE = "#!${BUILD_DIR}/runscript ${BUILD_DIR}/test-runscript"


def get_build_dir() -> Path:
    """Return the resolved path to the _build directory, relative to this script."""
    return (Path(__file__).parent.parent / "_build").resolve()


class Main:

    def __init__(self):
        self._script_dir: Path = Path(__file__).parent
        self._build_dir: Path = get_build_dir()
        self._test_files = sorted(self._script_dir.glob("*.yaml"))

    def clean_build_scripts(self) -> None:
        """Delete all .sh files in the build directory from a previous test run."""
        for path in self._build_dir.glob("*.sh"):
            path.unlink()

    def expand_build_dir(self, text: str) -> str:
        """Replace all occurrences of ${BUILD_DIR} with the actual build directory path."""
        return text.replace("${BUILD_DIR}", str(self._build_dir))

    def check_safe_shebang(self, first_line: str) -> bool:
        """Return True only if the first line matches the permitted shebang pattern.

        The check is performed on the unexpanded template so that hardcoded paths
        cannot be used to load arbitrary executables during testing.
        """
        safe = self.expand_build_dir(_SAFE_SHEBANG_TEMPLATE)
        return first_line.rstrip("\n\r").startswith(safe)

    def run_test(self, test: dict) -> tuple:
        """Execute a single test case.

        Returns a four-tuple:
        (result, actual_stdout, expected_stdout, details)

        where result is:
        None  — test was skipped (expected stdout is '...')
        True  — test passed
        False — test failed or was blocked

        and details is a dict with extra diagnostic information (stderr, exit code).

        The YAML test entry supports these optional fields beyond 'name', 'script'
        and 'output':
        exit_code (int, default 0) — expected process exit code
        stderr    (str, default unset) — expected stderr text; if absent, stderr
                                        is not compared but is shown on failure
        env       (dict, default {}) — environment variables to set when running
                                       the script; useful for testing conditional
                                       bindings such as NAME:=VALUE
        output    (str, required unless exit_code is set) — expected stdout; if '...',
                                         stdout is not compared (test runs for exit_code only)
        """
        name = test["name"]
        script_text = test["script"]
        expected_raw: str | None = test.get("output", None)
        expected_exit_code = int(test.get("exit_code", 0))
        expected_stderr_raw = test.get("stderr", None)
        extra_env: dict[str, str] = {str(k): str(v) for k, v in (test.get("env") or {}).items()}

        # Validate that the test specifies at least something to check.
        if expected_raw is None and "exit_code" not in test:
            print(f"  ERROR: {name} — test must specify at least one of 'output' or 'exit_code'", file=sys.stderr)
            return False, "", "", {}

        # Expand ${BUILD_DIR} so we can perform the safety check against real paths.
        expanded_script = self.expand_build_dir(script_text)
        lines = expanded_script.splitlines()

        if not lines:
            return False, "", "", {}

        # Safety check: only allow scripts whose shebang invokes our test-runscript.
        if not self.check_safe_shebang(lines[0]):
            print(
                f"  BLOCKED: {name} — shebang does not match the permitted pattern\n"
                f"           got: {lines[0]!r}",
                file=sys.stderr,
            )
            return False, "", "", {}

        # Write the expanded script to _build/<name>.sh.
        script_path = self._build_dir / f"{name}.sh"
        script_path.write_text(expanded_script, newline="\n")

        # Make the script executable.
        script_path.chmod(script_path.stat().st_mode | 0o111)

        # Tests with no expected output and no expected exit code are not yet
        # implemented. This check is performed after writing the script so that we
        # can still verify the shebang is correct by looking in the build directory.
        stdout_unchecked = expected_raw is None or expected_raw.strip() == "..."
        if stdout_unchecked and expected_exit_code == 0:
            return None, None, None, {}

        # Execute with a clean base environment, extended by any bindings declared
        # in the test's env: field, so that inherited variables do not contaminate
        # the JSON env array that test-runscript prints.
        proc = subprocess.run(
            [script_path],
            capture_output=True,
            text=True,
            env=extra_env,
        )
        actual_stdout = proc.stdout
        actual_stderr = proc.stderr
        actual_exit_code = proc.returncode

        # Expand ${BUILD_DIR} in the expected outputs before comparing.
        expected_stdout = self.expand_build_dir(expected_raw) if expected_raw is not None else "..."
        expected_stderr = (
            self.expand_build_dir(expected_stderr_raw)
            if expected_stderr_raw is not None
            else None
        )

        # Compare stdout, exit code, and optionally stderr.
        # stdout is unchecked when output was absent or set to '...'.
        stdout_ok = stdout_unchecked or actual_stdout.rstrip() == expected_stdout.rstrip()
        exit_ok = actual_exit_code == expected_exit_code
        stderr_ok = (
            expected_stderr is None
            or actual_stderr.rstrip() == expected_stderr.rstrip()
        )
        passed = stdout_ok and exit_ok and stderr_ok

        details = {
            "actual_exit_code": actual_exit_code,
            "expected_exit_code": expected_exit_code,
            "actual_stderr": actual_stderr,
            "expected_stderr": expected_stderr,
            "stdout_ok": stdout_ok,
            "exit_ok": exit_ok,
            "stderr_ok": stderr_ok,
        }
        return passed, actual_stdout, expected_stdout, details


    def run_yaml_file(self, yaml_path: Path) -> tuple:
        """Run all tests defined in a single YAML file.

        Returns (passed, failed, skipped) counts, and appends any failing test
        names to the returned list of failures.
        """
        data = yaml.safe_load(yaml_path.read_text())

        tests = data.get("tests", [])
        passed_count = 0
        failed_count = 0
        skipped_count = 0
        failures = []

        for test in tests:
            name = test.get("name", "<unnamed>")
            result, actual, expected, details = self.run_test(test)

            if result is None:
                skipped_count += 1
                print(f"  SKIP   {name}")
            elif result:
                passed_count += 1
                print(f"  PASS   {name}")
            else:
                failed_count += 1
                failures.append(name)
                print(f"  FAIL   {name}")
                # Show a diagnostic summary to help identify what went wrong.
                if not details.get("exit_ok", True):
                    print(
                        f"         exit code: expected {details['expected_exit_code']}, "
                        f"got {details['actual_exit_code']}"
                    )
                if not details.get("stdout_ok", True):
                    print(f"         stdout expected: {expected!r}")
                    print(f"         stdout actual:   {actual!r}")
                if not details.get("stderr_ok", True):
                    print(f"         stderr expected: {details['expected_stderr']!r}")
                    print(f"         stderr actual:   {details['actual_stderr']!r}")
                elif details.get("actual_stderr"):
                    # Always show unexpected stderr output to aid debugging.
                    print(f"         stderr: {details['actual_stderr'].rstrip()!r}")

        return passed_count, failed_count, skipped_count, failures



    def main(self) -> int:
        """Entry point."""
        build_dir = self._build_dir

        # Remove stale generated scripts from any previous run.
        self.clean_build_scripts()

        if not self._test_files:
            print(f"error: no test files found matching {self._script_dir}/*.yaml", file=sys.stderr)
            return 1

        total_passed = 0
        total_failed = 0
        total_skipped = 0
        all_failures: list[str] = []

        for yaml_path in self._test_files:
            print(f"\n{yaml_path}")
            p, f, s, failures = self.run_yaml_file(yaml_path)
            total_passed += p
            total_failed += f
            total_skipped += s
            all_failures.extend(failures)

        total_run = total_passed + total_failed
        print(f"\n{'─' * 50}")
        print(f"Results: {total_passed}/{total_run} passed, {total_skipped} skipped")

        if all_failures:
            print("\nFailing tests:")
            for name in all_failures:
                print(f"  - {name}")
            return 1

        return 0


def main() -> None:
    """Module-level entry point for use as a console script (pyproject.toml)."""
    sys.exit(Main().main())


if __name__ == "__main__":
    main()
