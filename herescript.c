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

// All mutable state for a single herescript invocation.
typedef struct {
    StringArray  arguments;
    char        *script_path;      // Resolved (realpath) path to the script file.
    char        *executable;       // The interpreter to exec.
    char       **user_params;      // Points into argv at the first user-supplied argument.
    int          user_param_count; // Number of user-supplied arguments.
} RunState;

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

static char *strndup_safe(const char *s, size_t n) {
    char *dup = strndup(s, n);
    if (!dup) {
        perror("strndup");
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

// A builder for the equivalent of Option<Token>. 
typedef struct {
    bool is_token; // True if this builder will generate a token, false if not.
    char  *data;
    size_t len;
    size_t cap;
} MaybeToken;

static void maybe_token_init(MaybeToken *b, size_t initial_cap) {
    b->is_token = false;
    b->data = malloc(initial_cap);
    if (!b->data) { perror("malloc"); exit(EXIT_GENERAL_ERROR); }
    b->len = 0;
    b->cap = initial_cap;
}

static void maybe_token_append(MaybeToken *b, char c) {
    if (b->len + 1 >= b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
        if (!b->data) { perror("realloc"); exit(EXIT_GENERAL_ERROR); }
    }
    b->data[b->len++] = c;
    b->is_token = true;
}

// Returns the accumulated token as a strdup'd NUL-terminated string.
static char *maybe_token_take(MaybeToken *b) {
    b->data[b->len] = '\0';
    char *s = strdup_safe(b->data);
    b->len = 0;
    b->is_token = false;
    return s;
}

static void maybe_token_free(MaybeToken *b) {
    free(b->data);
    b->data = NULL;
}

// Append the character produced by a backslash escape sequence. The argument
// is the character immediately following the backslash. Unrecognised escapes
// emit the backslash and the character literally.
static void maybe_token_append_escape(MaybeToken *b, char c) {
    b->is_token = true;
    switch (c) {
        case '\\': maybe_token_append(b, '\\'); break;
        case '\'': maybe_token_append(b, '\''); break;
        case '"':  maybe_token_append(b, '"');  break;
        case 's':  maybe_token_append(b, ' ');  break;
        case 'n':  maybe_token_append(b, '\n'); break;
        case 't':  maybe_token_append(b, '\t'); break;
        case 'r':  maybe_token_append(b, '\r'); break;
        default:
            maybe_token_append(b, '\\');
            maybe_token_append(b, c);
            break;
    }
}

// Mark the builder as containing a token, even if no characters have been appended.
static void maybe_token_is_token(MaybeToken *b) {
    b->is_token = true;
}

// ============================================================================
// RunState
// ============================================================================

static void flush_token(RunState *rs, MaybeToken *buf) {
    // Flush any partially-built token that precedes the slice.
    if (buf->is_token) {
        append_string_array(&rs->arguments, maybe_token_take(buf));
    }
}

// Emit one argument for each parameter in the half-open range [slice_a, slice_b).
// Parameter 0 is the script path; parameters 1..user_param_count are the
// user-supplied arguments; parameters beyond that expand to empty strings.
// Any partially-accumulated content in buf is flushed as a separate argument
// before the slice elements are emitted.
static void expand_slice(RunState *rs, MaybeToken *buf, int slice_a, int slice_b) {
    flush_token(rs, buf);
    for (int i = slice_a; i < slice_b; i++) {
        const char *value;
        if (i == 0) {
            value = rs->script_path;
        } else if (i <= rs->user_param_count) {
            value = rs->user_params[i - 1];
        } else {
            value = "";
        }
        append_string_array(&rs->arguments, strdup_safe(value));
    }
}

// Parse and expand a slice notation string of the form A:B (already extracted
// from ${A:B}, with the colon guaranteed present). A defaults to 0; B defaults
// to total argc. Both bounds are clamped to the valid range. Does not free name.
static void expand_slice_notation(RunState *rs, MaybeToken *buf, char *name) {
    int total_argc = rs->user_param_count + 1;
    int slice_a = 0;
    int slice_b = total_argc;
    char *colon = strchr(name, ':');
    *colon = '\0';  // Split the name at the colon.
    if (*name        != '\0') slice_a = atoi(name);
    if (*(colon + 1) != '\0') slice_b = atoi(colon + 1);
    // Clamp to valid range.
    if (slice_a < 0)          slice_a = 0;
    if (slice_b > total_argc) slice_b = total_argc;
    if (slice_a > slice_b)    slice_a = slice_b;
    expand_slice(rs, buf, slice_a, slice_b);
}

// Resolve and append a scalar ${NAME} or ${N} substitution to buf. Numeric names
// are treated as positional parameters; ${HERESCRIPT_FILE} is a synonym for ${0};
// all other names are looked up in the environment (fatal if unset).
static void expand_scalar_name(RunState *rs, MaybeToken *buf, const char *name) {
    size_t name_len = strlen(name);
    bool is_numeric = (name_len > 0);
    for (size_t k = 0; k < name_len && is_numeric; k++) {
        if (!isdigit((unsigned char)name[k])) is_numeric = false;
    }
    const char *value;
    if (is_numeric) {
        int param_num = atoi(name);
        if (param_num == 0) {
            // ${0} is the script path (via realpath), synonymous with ${HERESCRIPT_FILE}.
            value = rs->script_path;
        } else if (param_num <= rs->user_param_count) {
            value = rs->user_params[param_num - 1];
        } else {
            // Out-of-range parameter expands to the empty string.
            value = "";
        }
    } else if (strcmp(name, "HERESCRIPT_FILE") == 0) {
        // ${HERESCRIPT_FILE} is a synonym for ${0}. Setting it in the environment
        // is deferred to Step 2g; here we resolve it directly so it works in #:
        // lines without polluting the process environment.
        value = rs->script_path;
    } else {
        value = getenv_or_fail(name);
    }
    maybe_token_is_token(buf);
    for (const char *v = value; *v; v++) {
        maybe_token_append(buf, *v);
    }
}

// Parse a $'...' escape-quoted span. Called after consuming the $' prefix.
// Backslash escapes are processed; there is no variable interpolation.
// Advances *p past the closing single quote.
static void scan_dollar_single_quote(MaybeToken *b, const char **cursor) {
    while (**cursor && **cursor != '\'') {
        if (**cursor == '\\' && *(*cursor + 1)) {
            (*cursor)++;  // Skip backslash.
            maybe_token_append_escape(b, *(*cursor)++);
        } else {
            maybe_token_append(b, *(*cursor)++);
        }
    }
    if (**cursor == '\'') {
        (*cursor)++;  // Skip closing quote.
    } else {
        fprintf(stderr, "herescript: unterminated $' string in #: line\n");
        maybe_token_free(b);
        exit(EXIT_INVALID_HEADER);
    }
}

// Parse a '...' plain single-quoted span. Called after consuming the opening quote.
// No escapes or substitutions are performed; content is appended literally.
// Advances *p past the closing single quote.
static void scan_single_quote(MaybeToken *b, const char **cursor) {
    while (**cursor && **cursor != '\'') maybe_token_append(b, *(*cursor)++);
    if (**cursor == '\'') {
        (*cursor)++;  // Skip closing quote.
    } else {
        fprintf(stderr, "herescript: unterminated single quote in #: line\n");
        maybe_token_free(b);
        exit(EXIT_INVALID_HEADER);
    }
}

// Parse and expand a ${...} substitution. Called after consuming the ${ prefix.
// Handles both slice syntax (${A:B}) and scalar substitution (${N} or ${NAME}).
// Advances *p past the closing brace.
static void expand_dollar_brace(RunState *rs, MaybeToken *buf, const char **cursor) {
    const char *name_start = *cursor;
    while (**cursor && **cursor != '}') (*cursor)++;
    if (!**cursor) {
        fprintf(stderr, "herescript: unterminated ${ in #: line\n");
        maybe_token_free(buf);
        exit(EXIT_INVALID_HEADER);
    }
    char *name = strndup_safe(name_start, (size_t)(*cursor - name_start));
    (*cursor)++;  // Skip }.

    char *colon = strchr(name, ':');
    if (colon != NULL) {
        expand_slice_notation(rs, buf, name);
    } else {
        expand_scalar_name(rs, buf, name);
    }
    free(name);
}

// Process a #: arguments line using shell-like tokenisation. Tokens are
// separated by whitespace; quoting and substitution forms are dispatched to
// dedicated helpers. Steps 2a–2f are handled here collectively.
static void process_colon_line(RunState *rs, const char *line) {
    const char *cursor = line + 2;  // Skip the "#:" prefix.
    MaybeToken buf;
    maybe_token_init(&buf, 64);

    while (*cursor) {
        // Skip inter-token whitespace.
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) break;

        // Accumulate one token, dispatching on the leading character(s).
        while (*cursor && !isspace((unsigned char)*cursor)) {
            if (*cursor == '$' && *(cursor + 1) == '@') {
                cursor += 2;
                expand_slice(rs, &buf, 1, rs->user_param_count + 1);
            } else if (*cursor == '$' && *(cursor + 1) == '\'') {
                cursor += 2;
                scan_dollar_single_quote(&buf, &cursor);
            } else if (*cursor == '$' && *(cursor + 1) == '{') {
                cursor += 2;
                expand_dollar_brace(rs, &buf, &cursor);
            } else if (*cursor == '\'') {
                cursor++;
                scan_single_quote(&buf, &cursor);
            } else {
                maybe_token_append(&buf, *cursor++);
            }
        }

        // Flush any remaining content as a token. Slice expansions flush the
        // buffer themselves, so a standalone slice leaves nothing to flush here.
        flush_token(rs, &buf);
    }

    maybe_token_free(&buf);
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

    // Initialize run state.
    RunState rs;
    init_string_array(&rs.arguments, 16);
    rs.script_path = NULL;
    rs.executable = NULL;
    rs.user_params = NULL;
    rs.user_param_count = 0;

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
    rs.script_path = resolved_path;

    // Store user-supplied arguments so that ${N} (Step 2e) can expand them.
    // Note: ${HERESCRIPT_FILE} is set in the environment in Step 2g; for now it
    // is handled as a special name inside process_colon_line without polluting
    // the process environment.
    rs.user_params = (argc > 3) ? &argv[3] : NULL;
    rs.user_param_count = (argc > 3) ? argc - 3 : 0;

    // Open script file.
    FILE *fp = fopen(rs.script_path, "r");
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
    
    rs.executable = strdup_safe(exec_part);
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
            process_colon_line(&rs, line);
            continue;
        }
        
        // Any other line (including old-style #! continuation lines) ends the header block.
        break;
    }
    
    free(line);
    fclose(fp);

    // Build argv[].
    size_t new_argc = 1 + rs.arguments.count;
    char **new_argv = malloc((new_argc + 1) * sizeof(char *));
    if (!new_argv) {
        perror("malloc");
        return EXIT_GENERAL_ERROR;
    }
    
    new_argv[0] = rs.executable;
    for (size_t i = 0; i < rs.arguments.count; i++) {
        new_argv[i + 1] = rs.arguments.items[i];
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
    if (strchr(rs.executable, '/')) {
        execve(rs.executable, new_argv, environ);
    } else {
        execvp(rs.executable, new_argv);
    }
    
    // If we reach here, exec failed.
    perror("herescript: exec");
    fprintf(stderr, "  Hint: Ensure '%s' is installed and accessible.\n", rs.executable);
    return EXIT_EXEC_FAILURE;
}
