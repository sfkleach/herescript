# Task: in-file options for herescript, 2026-04-18

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

### Part A

- `run_state_process_bang_line` has become too big and straggly. 
- The interior of the while-loop should be extracted to a helper function
  that processes options.

### Part B

- There is repetition in the way long options with an argument is processed
- We should extract a helper function that handles `=` properly. I suggest
  something like the following:

```c
int handle_argument(size_t opt_len, char * tok_start, char **argument) {
    *argument = NULL;
    if (tok_len > opt_len && tok_start[opt_len] == '=') {
        *argument = strndup_safe(tok_start + opt_len + 1, tok_len - opt_len - 1);
    } else if (tok_len == opt_len) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) {
            return EXIT_INVALID_HEADER;
        }
        const char *val_start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
        *argument = strndup_safe(val_start, (size_t)(cursor - val_start));
    } else {
        return EXIT_INVALID_HEADER;
    }
}
```


## Final Step: Definition of Done

Review the [definition of done](../definition-of-done.md).

Verify that the CHANGELOG.md and README.md files are up to date with these
changes. Ensure that `just test` runs cleanly.

