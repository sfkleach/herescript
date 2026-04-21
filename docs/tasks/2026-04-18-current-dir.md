# Task: in-file options for herescript

The concept is that we can set options that customise herescript. These
appear in the script after a header-line like this:

```
#! --chdir DIRECTORY
```

The `#!` lines may appear anywhere in the initial header-lines section of
the file and are processed immediately. The arguments are subject to the usual 
tokenisation rule and must be supplied on the same line as the option itself.
Multiple options can be provided on the same line.

Mandatory single arguments may be passed as a standalone argument or by the
pattern `--OPTION=ARGUMENT`. (This is part of a wider convention that I adhere
to.)

## Step 1: --chdir DIRECTORY

This option is used for setting the current working directory. It takes 1
mandatory argument, which may be written as a standalone value on the same line
or following an `=` sign.

Examples:
```
## Equivalent forms:
#! --chdir /var/lib/myapp
#! --chdir=/var/lib/myapp
```

Implement this option, add a suitable functional test and update the documentation accordingly.

## Step 2: --dry-run

This option takes no argument and disables the normal execution. It print the
final execve arguments and environment without running; invaluable for
diagnosing header expansion.

Implement this option, add a suitable functional test and update the documentation accordingly.


## Step 3: --load-file FILE

This option loads a file before exec. Processes the lines in the env file as if
they were `#:` lines. This effectively combines both `.env` file and
option-loading.

Also acceptable:
```
#! --source=FILE
```

Implement this option, add a suitable functional test and update the documentation accordingly.


## Step 4: --path-prepend DIRECTORY

Prepend DIRECTORY to PATH; the canonical way to activate a virtualenv or local
toolchain without sourcing shell scripts. 

Also acceptable:
```
#! --path-prepend=DIRECTORY
```

Important:
- The DIRECTORY is checked that it exists.
- It is converted to an absolute path before being prepended.

Implement this option, add a suitable functional test and update the documentation accordingly.

## Step 5: --umask MASK

This option sets the file creation mask before exec; useful for scripts that
create files.

Implement this option, add a suitable functional test and update the documentation accordingly.

## Step 6: --unset VAR

This option removes an inherited env var before exec (scrubbing sensitive vars
from child). If the variable does not exist, this is not an error.

```
#! --unset=VAR
```

Implement this option, add a suitable functional test and update the documentation accordingly.

### Step 7: Tidy up option processing

... to be completed

## Final Step: Definition of Done

Review the [definition of done](../definition-of-done.md).

Verify that the CHANGELOG.md and README.md files are up to date with these
changes. Ensure that `just test` runs cleanly.

