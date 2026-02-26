#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>

// External environment variable (for execve).
extern char **environ;

// Exit codes.
#define EXIT_GENERAL_ERROR 1
#define EXIT_UNDEFINED_VAR 2
#define EXIT_MALFORMED_SHEBANG 3
#define EXIT_INVALID_HEADER 4
#define EXIT_EXEC_FAILURE 5

// Dynamic array for strings.
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringArray;

// Global state.
static StringArray arguments;
static char *script_path = NULL;
static char *executable = NULL;
static char **user_params = NULL;  // Points into argv at the first user-supplied argument.
static int user_param_count = 0;   // Number of user-supplied arguments.

// ============================================================================
// Dynamic Array Utilities
// ============================================================================

static void init_string_array(StringArray *arr, size_t initial_capacity) {
    arr->items = malloc(initial_capacity * sizeof(char *));
    if (!arr->items) {
        perror("malloc");
        exit(EXIT_GENERAL_ERROR);
    }
    arr->count = 0;
    arr->capacity = initial_capacity;
}

static void append_string_array(StringArray *arr, char *str) {
    if (arr->count >= arr->capacity) {
        arr->capacity *= 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(char *));
        if (!arr->items) {
            perror("realloc");
            exit(EXIT_GENERAL_ERROR);
        }
    }
    arr->items[arr->count++] = str;
}

// ============================================================================
// String Utilities
// ============================================================================

static char *strdup_safe(const char *s) {
    char *dup = strdup(s);
    if (!dup) {
        perror("strdup");
        exit(EXIT_GENERAL_ERROR);
    }
    return dup;
}

// ============================================================================
// Environment Variable Lookup
// ============================================================================

static const char *getenv_or_fail(const char *name) {
    const char *value = getenv(name);
    if (!value) {
        fprintf(stderr, "herescript: undefined environment variable: ${%s}\n", name);
        fprintf(stderr, "  Hint: Ensure the variable is set before running this script.\n");
        exit(EXIT_UNDEFINED_VAR);
    }
    return value;
}

// ============================================================================
// New-style Header Line Parsing (#:, #|)
// ============================================================================

// Growable byte buffer for token construction.
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} ByteBuf;

static void bytebuf_init(ByteBuf *b, size_t initial_cap) {
    b->data = malloc(initial_cap);
    if (!b->data) { perror("malloc"); exit(EXIT_GENERAL_ERROR); }
    b->len = 0;
    b->cap = initial_cap;
}

static void bytebuf_append(ByteBuf *b, char c) {
    if (b->len + 1 >= b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
        if (!b->data) { perror("realloc"); exit(EXIT_GENERAL_ERROR); }
    }
    b->data[b->len++] = c;
}

// Returns a strdup'd NUL-terminated copy of the accumulated bytes and resets
// the length to zero so the buffer can be reused for the next token.
static char *bytebuf_take(ByteBuf *b) {
    b->data[b->len] = '\0';
    char *s = strdup_safe(b->data);
    b->len = 0;
    return s;
}

static void bytebuf_free(ByteBuf *b) {
    free(b->data);
    b->data = NULL;
}

// Append the character produced by a backslash escape sequence. The argument
// is the character immediately following the backslash. Unrecognised escapes
// emit the backslash and the character literally.
static void bytebuf_append_escape(ByteBuf *b, char c) {
    switch (c) {
        case '\\': bytebuf_append(b, '\\'); break;
        case '\'': bytebuf_append(b, '\''); break;
        case '"':  bytebuf_append(b, '"');  break;
        case 's':  bytebuf_append(b, ' ');  break;
        case 'n':  bytebuf_append(b, '\n'); break;
        case 't':  bytebuf_append(b, '\t'); break;
        case 'r':  bytebuf_append(b, '\r'); break;
        default:
            bytebuf_append(b, '\\');
            bytebuf_append(b, c);
            break;
    }
}

// Process a #: arguments line using shell-like tokenisation.
// Step 2c: $'...' enables backslash escape processing inside a single-quoted
// span. Plain '...' spans remain literal (no escapes). Both forms concatenate
// with adjacent unquoted or quoted text into a single token.
// Step 2d: ${NAME} outside quotes expands to the value of environment variable
// NAME. An undefined variable is a fatal error. The expanded text is appended
// to the current token without further word-splitting.
// Step 2e: ${0} expands to the script path (via realpath), synonymous with
// ${HERESCRIPT_FILE}. ${N} for N >= 1 expands to the N-th user-supplied
// argument (i.e. the arguments passed after the script name on the command
// line). Out-of-range parameters expand to the empty string.
// Step 2f: ${A:B} is a parameter slice that expands to B-A separate arguments.
// A defaults to 0 and B defaults to argc (user_param_count + 1). $@ is a
// synonym for ${1:}.

// Emit one argument for each parameter in the half-open range [slice_a, slice_b).
// Parameter 0 is the script path; parameters 1..user_param_count are the
// user-supplied arguments; parameters beyond that expand to empty strings.
// Any partially-accumulated content in buf is flushed as a separate argument
// before the slice elements are emitted.
static void expand_slice(ByteBuf *buf, int slice_a, int slice_b) {
    // Flush any partially-built token that precedes the slice.
    if (buf->len > 0) {
        append_string_array(&arguments, bytebuf_take(buf));
    }
    for (int i = slice_a; i < slice_b; i++) {
        const char *value;
        if (i == 0) {
            value = script_path;
        } else if (i <= user_param_count) {
            value = user_params[i - 1];
        } else {
            value = "";
        }
        append_string_array(&arguments, strdup_safe(value));
    }
}

static void process_colon_line(const char *line) {
    // Skip the "#:" prefix.
    const char *p = line + 2;
    ByteBuf buf;
    bytebuf_init(&buf, 64);

    while (*p) {
        // Skip inter-token whitespace.
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        // Accumulate one token.
        // has_scalar is true when at least one scalar (non-slice) expression has
        // contributed to this token. This ensures that a scalar expansion producing
        // an empty string (e.g. an out-of-range ${N}) still yields an empty-string
        // argument, while a standalone slice expansion yields nothing extra.
        bool has_scalar = false;
        while (*p && !isspace((unsigned char)*p)) {
            if (*p == '$' && *(p + 1) == '@') {
                // $@ is a synonym for ${1:} — all user-supplied arguments as separate tokens.
                p += 2;
                expand_slice(&buf, 1, user_param_count + 1);
            } else if (*p == '$' && *(p + 1) == '\'') {
                // Escape-enabled single-quoted span: $'...'.
                // Backslash escapes are processed; no interpolation.
                p += 2;  // Skip $'.
                while (*p && *p != '\'') {
                    if (*p == '\\' && *(p + 1)) {
                        p++;  // Skip backslash.
                        bytebuf_append_escape(&buf, *p++);
                    } else {
                        bytebuf_append(&buf, *p++);
                    }
                }
                if (*p == '\'') {
                    p++;  // Skip closing quote.
                } else {
                    fprintf(stderr, "herescript: unterminated $' string in #: line\n");
                    bytebuf_free(&buf);
                    exit(EXIT_INVALID_HEADER);
                }
            } else if (*p == '$' && *(p + 1) == '{') {
                // Variable or parameter substitution: ${NAME} or ${N}.
                p += 2;  // Skip ${.
                const char *name_start = p;
                while (*p && *p != '}') p++;
                if (!*p) {
                    fprintf(stderr, "herescript: unterminated ${ in #: line\n");
                    bytebuf_free(&buf);
                    exit(EXIT_INVALID_HEADER);
                }
                size_t name_len = (size_t)(p - name_start);
                char *name = malloc(name_len + 1);
                if (!name) { perror("malloc"); exit(EXIT_GENERAL_ERROR); }
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';
                p++;  // Skip }.

                const char *value;
                // Check for slice syntax: the name contains a colon.
                char *colon = strchr(name, ':');
                if (colon != NULL) {
                    // ${A:B} slice: expands to B-A separate arguments.
                    // A defaults to 0; B defaults to total argc (user_param_count + 1).
                    int total_argc = user_param_count + 1;
                    int slice_a = 0;
                    int slice_b = total_argc;
                    *colon = '\0';  // Split the name at the colon.
                    const char *a_str = name;
                    const char *b_str = colon + 1;
                    if (*a_str != '\0') slice_a = atoi(a_str);
                    if (*b_str != '\0') slice_b = atoi(b_str);
                    // Clamp to valid range.
                    if (slice_a < 0) slice_a = 0;
                    if (slice_b > total_argc) slice_b = total_argc;
                    if (slice_a > slice_b) slice_a = slice_b;
                    free(name);
                    expand_slice(&buf, slice_a, slice_b);
                    // expand_slice does not break the token loop; continue accumulating.
                } else {
                    // Scalar substitution: ${N} or ${NAME}.
                    // Check whether the name is a non-negative integer (parameter reference).
                    bool is_numeric = (name_len > 0);
                    for (size_t k = 0; k < name_len && is_numeric; k++) {
                        if (!isdigit((unsigned char)name[k])) is_numeric = false;
                    }
                    if (is_numeric) {
                        int param_num = atoi(name);
                        if (param_num == 0) {
                            // ${0} is the script path (via realpath), synonymous with ${HERESCRIPT_FILE}.
                            value = script_path;
                        } else if (param_num <= user_param_count) {
                            value = user_params[param_num - 1];
                        } else {
                            // Out-of-range parameter expands to an empty string.
                            value = "";
                        }
                    } else if (strcmp(name, "HERESCRIPT_FILE") == 0) {
                        // ${HERESCRIPT_FILE} is a synonym for ${0}. Setting it in the
                        // environment is deferred to Step 2g; here we resolve it directly
                        // so it works in #: lines without polluting the process environment.
                        value = script_path;
                    } else {
                        value = getenv_or_fail(name);
                    }
                    free(name);
                    has_scalar = true;
                    for (const char *v = value; *v; v++) bytebuf_append(&buf, *v);
                }
            } else if (*p == '\'') {
                // Plain single-quoted span: literal, no escapes.
                p++;  // Skip opening quote.
                while (*p && *p != '\'') bytebuf_append(&buf, *p++);
                if (*p == '\'') {
                    p++;  // Skip closing quote.
                } else {
                    fprintf(stderr, "herescript: unterminated single quote in #: line\n");
                    bytebuf_free(&buf);
                    exit(EXIT_INVALID_HEADER);
                }
            } else {
                bytebuf_append(&buf, *p++);
            }
        }

        // Only flush if there is remaining buffered content. A slice expansion
        // calls expand_slice which flushes the buffer itself, so after a
        // standalone slice the buffer will be empty and nothing should be added.
        if (buf.len > 0 || has_scalar) {
            append_string_array(&arguments, bytebuf_take(&buf));
        }
    }

    bytebuf_free(&buf);
}

// ============================================================================
// Main Program
// ============================================================================

int main(int argc, char **argv) {

    // for (int i = 0; i < argc; i++) {
    //     printf("Debug: Initial argv[%d]: %s\n", i, argv[i]);
    // }


    if (argc < 2) {
        fprintf(stderr, "herescript: no script specified\n");
        fprintf(stderr, "  Hint: This program is meant to be used as a shebang interpreter.\n");
        return EXIT_GENERAL_ERROR;
    }

    // Handle --help as the sole argument. Special case that does not overlap
    // with normal usage since there are always at least three arguments in 
    // normal usage (herescript, executable and script-path).
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf(
            "Usage: herescript <executable> <script> [args...]\n"
            "\n"
            "Herescript is a modern, structured interpreter launcher designed to extend\n"
            "the limited Unix shebang mechanism. Scripts begin with\n"
            "'#!/usr/bin/herescript EXECUTABLE' and use header lines to build the argument\n"
            "list. For example:\n"
            "\n"
            "    #!/usr/bin/herescript python3\n"
            "    ## A comment line (discarded).\n"
            "    #: --verbose ${0}\n"
            "\n"
            "Here '#: --verbose ${0}' passes --verbose and the script path as arguments.\n"
            "\n"
            "To learn more about the exact syntax go to https://github.com/sfkleach/herescript.\n"
            "It supports:\n"
            "  - Setting environment variables scoped to the script invocation.\n"
            "  - Placing options before, after, or interleaved with the script arguments.\n"
            "  - Controlling exactly where the script filename appears in the argument list.\n"
        );
        return 0;
    }

    // Initialize arrays.
    init_string_array(&arguments, 16);

    // The first argument is the path to the executable to be launched (from the shebang).
    const char *exec_part = argv[1];
    
    // Get script path and resolve to canonical path. Passing NULL as the second
    // argument to realpath causes it to allocate a suitably-sized buffer, avoiding
    // any dependency on PATH_MAX (which POSIX permits implementations to leave
    // undefined when no fixed path-length limit exists).
    const char *script_arg = argv[2];
    char *resolved_path = realpath(script_arg, NULL);
    if (!resolved_path) {
        perror("herescript: realpath");
        fprintf(stderr, "  Hint: Ensure the script file `%s` exists and is accessible.\n", script_arg);
        return EXIT_GENERAL_ERROR;
    }
    script_path = resolved_path;

    // Store user-supplied arguments so that ${N} (Step 2e) can expand them.
    // Note: ${HERESCRIPT_FILE} is set in the environment in Step 2g; for now it
    // is handled as a special name inside process_colon_line without polluting
    // the process environment.
    user_params = (argc > 3) ? &argv[3] : NULL;
    user_param_count = (argc > 3) ? argc - 3 : 0;

    // Open script file.
    FILE *fp = fopen(script_path, "r");
    if (!fp) {
        perror("herescript: fopen");
        fprintf(stderr, "  Hint: Ensure the script file `%s` exists and is readable.\n", script_arg);
        return EXIT_GENERAL_ERROR;
    }
    
    // Parse shebang line.
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len = getline(&line, &line_cap, fp);
    
    if (line_len < 0) {
        fprintf(stderr, "herescript: empty script file\n");
        fprintf(stderr, "  Hint: Script must start with a shebang line.\n");
        fclose(fp);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    // Remove trailing newline.
    if (line_len > 0 && line[line_len - 1] == '\n') {
        line[line_len - 1] = '\0';
        line_len--;
    }
    
    // Check shebang format: #!<herescript-path> <executable>
    // We don't check the herescript path (since it's ourself), just check the prefix.
    if (line_len < 2 || line[0] != '#' || line[1] != '!') {
        fprintf(stderr, "herescript: malformed shebang line\n");
        fprintf(stderr, "  Expected: a line starting with #!\n");
        fprintf(stderr, "  Got: %s\n", line);
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    // Find the first space character after #!
    const char *space = strchr(line + 2, ' ');
    if (!space) {
        fprintf(stderr, "herescript: no executable specified in shebang\n");
        fprintf(stderr, "  Hint: The shebang must specify an executable after the herescript path.\n");
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    if (strlen(exec_part) == 0) {
        fprintf(stderr, "herescript: no executable specified in shebang\n");
        fprintf(stderr, "  Hint: The shebang must specify an executable after #!/usr/bin/herescript.\n");
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    // Check for options (space in executable).
    if (strchr(exec_part, ' ') || strchr(exec_part, '\t')) {
        fprintf(stderr, "herescript: shebang contains options, which are not allowed\n");
        fprintf(stderr, "  Expected: #!/usr/bin/herescript <executable>\n");
        fprintf(stderr, "  Got: %s\n", line);
        fprintf(stderr, "  Hint: Options to the executable should be specified in header lines,\n");
        fprintf(stderr, "        not in the shebang line.\n");
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    executable = strdup_safe(exec_part);
    free(line);
    line = NULL;
    
    // Parse header lines.
    while ((line_len = getline(&line, &line_cap, fp)) >= 0) {
        // Remove trailing newline.
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
            line_len--;
        }
        
        // Check if this is a header line.
        if (line_len < 2 || line[0] != '#') {
            break;  // End of header block.
        }
        
        if (line[1] == '#') {
            // New-style comment line: discard.
            continue;
        }

        if (line[1] == ':') {
            // New-style arguments line: shell-like tokenisation.
            process_colon_line(line);
            continue;
        }
        
        // Any other line (including old-style #! continuation lines) ends the header block.
        break;
    }
    
    free(line);
    fclose(fp);

    // Build argv[].
    size_t new_argc = 1 + arguments.count;
    char **new_argv = malloc((new_argc + 1) * sizeof(char *));
    if (!new_argv) {
        perror("malloc");
        return EXIT_GENERAL_ERROR;
    }
    
    new_argv[0] = executable;
    for (size_t i = 0; i < arguments.count; i++) {
        new_argv[i + 1] = arguments.items[i];
    }
    new_argv[new_argc] = NULL;
    
    // Bindings have already been applied to the environment during parsing.
    // The environment is now ready for execution.

    // for (size_t i = 0; i < new_argc; i++) {
    //     printf("Debug: argv[%zu]: %s\n", i, new_argv[i]);
    // }
    
    // Execute. All memory allocated during this process — arguments, bindings,
    // script_path, executable, new_argv — is intentionally left unfreed. On a
    // successful exec the kernel replaces the process image entirely, so the
    // allocations simply cease to exist. On an error return main() exits
    // immediately afterward and the OS reclaims everything. There is no code
    // path in which this process continues running after this point, so
    // explicit teardown would be pure ceremony with no practical effect.
    //
    // If executable contains '/', use it as a direct path; otherwise search PATH.
    if (strchr(executable, '/')) {
        execve(executable, new_argv, environ);
    } else {
        execvp(executable, new_argv);
    }
    
    // If we reach here, exec failed.
    perror("herescript: exec");
    fprintf(stderr, "  Hint: Ensure '%s' is installed and accessible.\n", executable);
    return EXIT_EXEC_FAILURE;
}
