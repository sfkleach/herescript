# test-herescript.c

`test-herescript.c` is a small command-line C program that we use for testing the
behaviour of herescript. It accepts any number of arguments. It then writes to
stdout a JSON blob which displays the environment and arguments in this format:

```json
{
    "env": [
        BINDINGS
    ],
    "argv": [
        ARGUMENTS
    ]
}
```

For example, running `test-herescript foo bar gort` will output something like:

```json
{
    "env": [
        "SHELL=/bin/bash",
        "SESSION_MANAGER=local/Mirth:@/tmp/.ICE-unix/1760,unix/Mirth:/tmp/.ICE-unix/9876",
        "WINDOWID=123456789",
        "QT_ACCESSIBILITY=1",
        ...
    ],
    "argv": [
        "test-herescript",
        "foo",
        "bar",
        "gort"
    ]
}
```


## Part 1: Implement

- Implement test-herescript.c and add it to the Makefile as a separate build target
  `test-herescript`. The Justfile should compile both *.c files. The build should
  be placed in _build, similarly to herescript.
- The order of environment variables should be as received.
- Errors should be logged to stderr and a non-zero exit status code returned.