# The --help option

## Goal

If runscript is invoked with one and only one argument `--help` then it 
should print out the following useful summary.

```txt
Runscript is a modern, structured interpreter launcher designed to extend
the idiom of starting shebang scripts with #!/usr/bin/env. Instead scripts begin
with:

    #!/usr/bin/runscript <executable>

This is followed by a header block of lines each of which start with `#!`. To
learn more about the exact syntax go to https://github.com/sfkleach/runscript.
It supports:
  - locally defining environment variables,
  - controlling the order of options and arguments,
  - and choosing where to insert the script file-name itself.
```