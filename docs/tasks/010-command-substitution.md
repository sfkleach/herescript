# Sub-command execution, 2026-04-25

## Step 1: Implement a new header line `#|`

A new type of header line is introduced: `#| PATH ARGS... >${NAME}`. This
pipes the output of the last bound `#>` block through the named command and
binds the output to `${NAME}`. e.g.

```
#> datetime
#| bash >${NOW}
```

If the command returns a non-zero status the entire herescript command fails.

## Step 2: No `>${NAME}

Optionally the target environment variable be omitted, in which case it
simply executes the command and ignores the output. e.g.

```
#> print('Hello world')
#| python3
```

## Step 3: No subcommand

Optionally the command may be omitted, in which case the target environment 
variable is simply bound to the last output unchanged. e.g.

```
#> Mary has a little lamb
#> Its fleece was white as snow
#| >${POETRY}
```

## Step 4: No inline block

A slightly different header line `#$ PATH ARGS... >${NAME}` does away with
the need for a preceding `#>` block. It simply runs and binds the output.

```
#$ datetime >${NOW}
```

Like `#|` the target environment may be omitted, typically for the side 
effect of running the command. e.g.

```
#$ cp file1 file2
```

However the `PATH ARGS..` part cannot be omitted.

As before, if the sub-command fails, herescript does not attempt to continue
but stops with an appropriate message.


