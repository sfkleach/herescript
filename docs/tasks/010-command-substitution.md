# Sub-command execution, 2026-04-25

## Step 1: Implement a new header line `#|`

A new type of header line is introduced: `#| PATH ARGS... => NAME`. This
pipes the output of the last bound `#>` block through the named command and
binds the output to `$NAME`. e.g.

```
#> date
#| bash => NOW
```

Note that both `=>NAME` and `=> NAME` are accepted syntaxes for the target
variable. However the preceding whitespace is mandatory.

Note that `#|` is required to immediately follow a `#>` block (aka a run of
consectutive `#>` lines). This is not strictly required semantically but a
syntactic limitation to help readers understand the contents of a herescript
by grouping the heredoc with the command.

If the command returns a non-zero status the entire herescript command fails.

### Clarifications

Q: How is the captured value bound? The natural answer is setenv(), so ${NAME} substitution on subsequent #: lines just works. Worth stating explicitly because it's the foundation for using captured values
- A: setenv()

Q: What is the "Last bound #> block" definition.
- A: It is literally the highest numbered variable in the series HERESCRIPT1,
     HERESCRIPT2, HERESCRIPT3, ...

Q: Exit code on subcommand failure?
- A: Add a new exit code (e.g. EXIT_SUBCOMMAND_FAILURE = 6)

Q: stderr handling?
- A: Inherited (passes through to the terminal)

Q: Trailing newline on captured output. Shell $(cmd) strips trailing newlines. #|/#$ should match that convention (and it should be documented).
- A: Yes, good point.

Q: Tokenisation of PATH ARGS.... Are #|/#$ lines tokenised like #: lines (full quoting and substitution support)? 
- A: They are tokenised in the same way as `#:` lines, excluding the trailing `=>NAME`

Q: --dry-run interaction. If --dry-run is set, should #|/#$ still execute? 
- A: No.

Q: --load-file interaction. Should a loaded file be allowed to contain #|/#$? 
- A: No. The load file is merely a convenmience for `#:` lines without the `#:`.

Q: `#| => NAME` deduplication. If #| consumes a #> block by piping it through a command, is that block also still bound to HERESCRIPT<N>? I'd suggest no — the #| consumes the block, removing it from the implicit numbering. Otherwise you have the same content under two names, which is confusing.
- A: No, the two variables have the same content. Justification: because
  otherwise we have a rare special case that needs to be documented and, much
  worse, understood by the users.

Q: Does #|/#$ run at parse time (header processing) or exec time? The choice has wide-ranging consequences:
- A: 
    - all header effects are applied immediately at parse time, in the order they appear.
    - When we need to execute a subcommand, we must execute it using the state accumulated so far
    - This means a subcommand can only "see" the options and bindings that precede it.

Comment: Currently herescript's threat model is "fork/exec once, at the end". This proposal changes that. Worth stating explicitly.
- Agreed. The threat model is changed.

## Step 2: Skip `=> NAME`

Optionally the target environment variable may be omitted, in which case it
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
#| => POETRY
```

### Clarifications

Q: Stdin for #$ (no preceding #>)? Three reasonable choices: /dev/null, inherited from herescript's stdin, or unspecified. /dev/null is the safest default — it prevents subcommands from accidentally hanging on terminal stdin.
- A: /dev/null is perfect.

Q: Stdout when no => NAME? For #$ cp file1 file2 (side-effect command), is stdout inherited (passes to terminal) or discarded? Inherited matches the stderr decision and is least surprising.
- A: Inherited



## Step 4: No inline block

A slightly different header line `#$ PATH ARGS... => NAME` does away with
the need for a preceding `#>` block. It simply runs and binds the output.

```
#$ date => NOW
```

Like `#|` the target environment may be omitted, typically for the side 
effect of running the command. e.g.

```
#$ cp file1 file2
```

However the `PATH ARGS..` part cannot be omitted.

As before, if the sub-command fails, herescript does not attempt to continue
but stops with an appropriate message.

### Clarifications

Comment: Step 4 says "the PATH ARGS.. part cannot be omitted" — good, but be explicit that #$ with no command is a parse error.
- Agreed. Omitting the command is a parse error.

## Step 5: Refactor sub-command functions

The functions run_state_process_pipe_line and run_state_process_dollar_line have
a good deal of common code. Both the initial and final parts are almost identical
and it suggests we can extract a common helper function for the start and ends
of the two functions.


## Final Step: Definition of Done

Document the new threat model in CHANGELOG / README. Something like: "headers
with #| or #$ execute commands during script load; do not run herescript scripts
from untrusted sources." This is a real expansion of what running a herescript
script does, and it deserves a prominent note.

Review the [definition of done](../definition-of-done.md).

Verify that the CHANGELOG.md and README.md files are up to date with these
changes. Ensure that `just test` runs cleanly.
