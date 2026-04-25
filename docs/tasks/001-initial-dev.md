# Initial development of herescript, 2026-01-11

`herescript` is a modern, structured interpreter launcher designed to extend the limited Unix shebang mechanism. It provides:

- multi‑line argument declarations
- environment bindings
- conditional environment initialisation
- strict, single‑pass substitution
- explicit escape semantics
- deterministic execution via `execve()`

## Part 1

Create the basic structure for a small C project that is intended to target Unix systems. The skeleton code looks something like this:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    // 1. Determine script path
    // 2. Open script
    // 3. Parse shebang
    // 4. Parse header lines
    //    - extract metachars
    //    - apply escapes
    //    - apply substitution
    //    - classify line
    // 5. Build argv[]
    // 6. Build envp[]
    // 7. execve()

    fprintf(stderr, "herescript: implementation pending\n");
    return 1;
}
```

It will be so simple that it is likely to be a single file application.

## Part 2

To be defined.