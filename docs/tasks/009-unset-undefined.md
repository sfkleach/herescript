# New option --unset-undefined, 2026-04-25-08:40

## Step 1

Ensure that the option `--unset=NAME` (or equivalently `--unset NAME`) validates
the NAME ahead of time rather than waits for `unsetenv()` to object.


## Step 2

Implement a new option `--unset-undefined=(error|warning|allow)` that changes the
behaviour of the `--unset=NAME` option when NAME is not defined. This option 
affects all uses of `--unset`, both those preceding and following.

- error: will generate an error message on stderr and halt the program (default).
- warning: will generate a message on stderr but allow exection to continue.
- allow: will skip any message and execution will continue.
- no other option is allowed.

As usual the `=` between the option and argument is optional i.e. you can
specify it as a single argument or two arguments.

## Final Step: Definition of Done

Review the [definition of done](../definition-of-done.md).

Verify that the CHANGELOG.md and README.md files are up to date with these
changes. Ensure that `just test` runs cleanly.
