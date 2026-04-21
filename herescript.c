#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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

// Maximum length accepted for the shebang line. The shebang is structurally
// bounded by #! + herescript-path + space + executable-path, so 3 + 2*PATH_MAX
// is a tight upper bound. This also defends against accidentally running
// herescript on a non-script file (e.g. a binary with no early newline).
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define MAX_LINE_LENGTH (3 + 2 * PATH_MAX)



// ============================================================================
// Dynamic Array Utilities
// ============================================================================

// Dynamic array for strings.
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringArray;

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

// Returns the internal array of strings. The returned object is owned by the 
// StringArray and should not be freed directly.
static char **string_array_borrow_data(StringArray const * arr) {
    return arr->items;
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
// New-style Header Line Parsing (#:)
// ============================================================================

// A builder for the equivalent of Option<Token>. 
typedef struct {
    bool is_token; // True if this builder will generate a token, false if not.
    char  *data;
    size_t len;
    size_t cap;
} MaybeToken;

static void maybe_token_init(MaybeToken *mt, size_t initial_cap) {
    mt->is_token = false;
    mt->data = malloc(initial_cap);
    if (!mt->data) {
        perror("malloc");
        exit(EXIT_GENERAL_ERROR);
    }
    mt->len = 0;
    mt->cap = initial_cap;
}

static void maybe_token_append(MaybeToken *mt, char c) {
    if (mt->len + 1 >= mt->cap) {
        mt->cap *= 2;
        mt->data = realloc(mt->data, mt->cap);
        if (!mt->data) {
            perror("realloc");
            exit(EXIT_GENERAL_ERROR);
        }
    }
    mt->data[mt->len++] = c;
    mt->is_token = true;
}

// Returns the accumulated token as a strdup'd NUL-terminated string.
static char *maybe_token_take(MaybeToken *mt) {
    mt->data[mt->len] = '\0';
    char *s = strdup_safe(mt->data);
    mt->len = 0;
    mt->is_token = false;
    return s;
}

static void maybe_token_free(MaybeToken *mt) {
    free(mt->data);
    mt->data = NULL;
}

// Append the character produced by a backslash escape sequence. The argument
// is the character immediately following the backslash. Unrecognised escapes
// emit the backslash and the character literally.
static void maybe_token_append_escape(MaybeToken *mt, char c) {
    mt->is_token = true;
    switch (c) {
        case '\\':
            maybe_token_append(mt, '\\');
            break;
        case '\'':
            maybe_token_append(mt, '\'');
            break;
        case '"':
            maybe_token_append(mt, '"');
            break;
        case 's':
            maybe_token_append(mt, ' ');
            break;
        case 'n':
            maybe_token_append(mt, '\n');
            break;
        case 't':
            maybe_token_append(mt, '\t');
            break;
        case 'r':
            maybe_token_append(mt, '\r');
            break;
        default:
            maybe_token_append(mt, '\\');
            maybe_token_append(mt, c);
            break;
    }
}

// Mark the builder as containing a token, even if no characters have been appended.
static void maybe_token_is_token(MaybeToken *mt) {
    mt->is_token = true;
}

// ============================================================================
// RunState
// ============================================================================

// All mutable state for a single herescript invocation.
typedef struct {
    StringArray  arguments;
    char        *script_path;      // Resolved (realpath) path to the script file.
    char        *executable;       // The interpreter to exec.
    char       **user_params;      // Points into argv at the first user-supplied argument.
    int          user_param_count; // Number of user-supplied arguments.
    int          inline_arg_count; // Counter for HERESCRIPT0, HERESCRIPT1, etc.
    char        *chdir_target;     // Directory to chdir into before exec, or NULL.
    bool         dry_run;          // If true, print exec args/env instead of running.
    mode_t       umask_value;      // File creation mask to apply before exec.
    bool         umask_set;        // True if --umask was specified.
    StringArray  unset_vars;       // Environment variables to unset before exec.
} RunState;

static void run_state_init(RunState *rs) {
    init_string_array(&rs->arguments, 16);
    rs->script_path = NULL;
    rs->executable = NULL;
    rs->user_params = NULL;
    rs->user_param_count = 0;
    rs->inline_arg_count = 0;
    rs->chdir_target = NULL;
    rs->dry_run = false;
    rs->umask_value = 0;
    rs->umask_set = false;
    init_string_array(&rs->unset_vars, 4);
}

// Bind HERESCRIPT_FILE in the process environment so that the subprocess
// inherits it and #: lines can reference it via the normal env-var path.
static void run_state_bind_herescript_file(RunState const * rs) {
    if (setenv("HERESCRIPT_FILE", rs->script_path, 1) != 0) {
        perror("herescript: setenv HERESCRIPT_FILE");
        exit(EXIT_GENERAL_ERROR);
    }
}

// Bind HERESCRIPT_COMMAND in the process environment so that the subprocess
// inherits the interpreter name from the shebang line.
static void run_state_bind_herescript_command(RunState const * rs) {
    if (setenv("HERESCRIPT_COMMAND", rs->executable, 1) != 0) {
        perror("herescript: setenv HERESCRIPT_COMMAND");
        exit(EXIT_GENERAL_ERROR);
    }
}

// Compare two C-string pointers for qsort.
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

// Print the planned execve arguments and environment to stdout for --dry-run.
// Output mirrors the JSON format produced by test-herescript for consistency.
static void run_state_print_dry_run(RunState const *rs) {
    // Count and sort environment variables for deterministic output.
    int env_count = 0;
    for (char **e = environ; *e != NULL; e++) env_count++;
    char **sorted_env = malloc((size_t)env_count * sizeof(char *));
    if (!sorted_env) { perror("malloc"); exit(EXIT_GENERAL_ERROR); }
    for (int i = 0; i < env_count; i++) sorted_env[i] = environ[i];
    qsort(sorted_env, (size_t)env_count, sizeof(char *), compare_strings);

    printf("{\n");
    printf("    \"env\": [\n");
    for (int i = 0; i < env_count; i++) {
        printf("        \"%s\"", sorted_env[i]);
        printf("%s\n", i < env_count - 1 ? "," : "");
    }
    printf("    ],\n");

    // argv[0] is the executable; remaining entries are rs->arguments.
    size_t argc = rs->arguments.count + 1;
    printf("    \"argv\": [\n");
    printf("        \"%s\"", rs->executable);
    if (rs->arguments.count > 0) printf(",");
    printf("\n");
    for (size_t i = 0; i < rs->arguments.count; i++) {
        printf("        \"%s\"", rs->arguments.items[i]);
        printf("%s\n", i < rs->arguments.count - 1 ? "," : "");
    }
    printf("    ]\n");
    printf("}\n");
    (void)argc;
    free(sorted_env);
}

// Build a NULL-terminated argv array from rs->executable followed by all
// accumulated arguments, then exec the interpreter. Does not return on success.
static int run_state_exec(RunState * rs) {
    if (rs->chdir_target != NULL) {
        if (chdir(rs->chdir_target) != 0) {
            fprintf(stderr, "herescript: --chdir: ");
            perror(rs->chdir_target);
            return EXIT_GENERAL_ERROR;
        }
    }

    if (rs->umask_set) {
        umask(rs->umask_value);
    }

    for (size_t i = 0; i < rs->unset_vars.count; i++) {
        unsetenv(rs->unset_vars.items[i]);
    }

    if (rs->dry_run) {
        run_state_print_dry_run(rs);
        return EXIT_SUCCESS;
    }

    // Capacity is count + 1 for the executable + 1 for the NULL sentinel.
    StringArray argv_arr;
    init_string_array(&argv_arr, rs->arguments.count + 2);
    append_string_array(&argv_arr, rs->executable);
    for (size_t i = 0; i < rs->arguments.count; i++) {
        append_string_array(&argv_arr, rs->arguments.items[i]);
    }
    append_string_array(&argv_arr, NULL);  // NULL sentinel required by execve/execvp.
    if (strchr(rs->executable, '/')) {
        execve(rs->executable, string_array_borrow_data(&argv_arr), environ);
    } else {
        execvp(rs->executable, string_array_borrow_data(&argv_arr));
    }
    // If we reach here, exec failed.
    perror("herescript: exec");
    fprintf(stderr, "  Hint: Ensure '%s' is installed and accessible.\n", rs->executable);
    return EXIT_EXEC_FAILURE;
}

static void run_state_flush_maybe_token(RunState * rs, MaybeToken *buf) {
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
static void expand_slice(RunState * rs, MaybeToken *buf, int slice_a, int slice_b) {
    run_state_flush_maybe_token(rs, buf);
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
static void expand_slice_notation(RunState * rs, MaybeToken *buf, char const * name) {
    int total_argc = rs->user_param_count + 1;
    int slice_a = 0;
    int slice_b = total_argc;
    char *colon = strchr(name, ':');
    *colon = '\0';  // Split the name at the colon.
    if (*name != '\0') {
        // Validate that the LHS is a non-negative integer before converting.
        for (const char *p = name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                fprintf(stderr, "herescript: slice index '%s' is not a non-negative integer.\n", name);
                exit(EXIT_GENERAL_ERROR);
            }
        }
        slice_a = atoi(name);
    }
    if (*(colon + 1) != '\0') {
        // Validate that the RHS is a non-negative integer before converting.
        for (const char *p = colon + 1; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                fprintf(stderr, "herescript: slice index '%s' is not a non-negative integer.\n", colon + 1);
                exit(EXIT_GENERAL_ERROR);
            }
        }
        slice_b = atoi(colon + 1);
    }
    // Clamp to valid range.
    if (slice_a < 0) {
        slice_a = 0;
    }
    if (slice_b > total_argc) {
        slice_b = total_argc;
    }
    if (slice_a > slice_b) {
        slice_a = slice_b;
    }
    expand_slice(rs, buf, slice_a, slice_b);
}

// Resolve and append a scalar ${NAME} or ${N} substitution to buf. Numeric names
// are treated as positional parameters; ${0} is a synonym for ${HERESCRIPT_FILE}
// (which is also set in the environment). All other names are looked up in the
// environment (fatal if unset).
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

// Common implementation for $'...' and $"..." escape-quoted spans.
// closing_quote: the character to terminate on (' or ").
static void scan_dollar(MaybeToken *mt, const char **cursor, char closing_quote) {
    while (**cursor && **cursor != closing_quote) {
        if (**cursor == '\\' && *(*cursor + 1)) {
            (*cursor)++;  // Skip backslash.
            maybe_token_append_escape(mt, *(*cursor)++);
        } else {
            maybe_token_append(mt, *(*cursor)++);
        }
    }
    if (**cursor == closing_quote) {
        (*cursor)++;  // Skip closing quote.
    } else {
        fprintf(stderr, "herescript: unterminated $%c string in #: line\n", closing_quote);
        maybe_token_free(mt);
        exit(EXIT_INVALID_HEADER);
    }
}

static void scan_dollar_single_quote(MaybeToken *mt, const char **cursor) {
    scan_dollar(mt, cursor, '\'');
}

static void scan_dollar_double_quote(MaybeToken *mt, const char **cursor) {
    scan_dollar(mt, cursor, '"');
}

// Parse a '...' plain single-quoted span. Called after consuming the opening quote.
// No escapes or substitutions are performed; content is appended literally.
// Advances *p past the closing single quote.
static void scan_single_quote(MaybeToken *mt, const char **cursor) {
    while (**cursor && **cursor != '\'') {
        maybe_token_append(mt, *(*cursor)++);
    }
    if (**cursor == '\'') {
        (*cursor)++;  // Skip closing quote.
    } else {
        fprintf(stderr, "herescript: unterminated single quote in #: line\n");
        maybe_token_free(mt);
        exit(EXIT_INVALID_HEADER);
    }
}

static bool is_first_name_char(char c) {
    return(
        isalpha((unsigned char)c) ||
        c == '_'
    );
}

static bool is_name_char(char c) {
    return(
        isalpha((unsigned char)c) ||
        c == '_' ||
        isdigit((unsigned char)c)
    );
}

// Return true when name (of length name_len) is a valid identifier: starts
// with a letter or underscore, followed by zero or more alphanumerics or
// underscores.
static bool is_identifier(const char *name, size_t name_len) {
    if (name_len == 0) return false;
    if (!is_first_name_char(name[0])) return false;
    for (size_t i = 1; i < name_len; i++) {
        if (!is_name_char(name[i])) return false;
    }
    return true;
}

// Parse and expand a ${...} substitution. Called after consuming the ${ prefix.
// Handles inline defaults (${NAME-VALUE} and ${NAME:=VALUE}), slice syntax
// (${A:B}), and scalar substitution (${N} or ${NAME}).
// Advances *p past the closing brace.
static void expand_dollar_brace(RunState *rs, MaybeToken *buf, const char **cursor) {
    const char *name_start = *cursor;
    while (**cursor && **cursor != '}') {
        (*cursor)++;
    }
    if (!**cursor) {
        fprintf(stderr, "herescript: unterminated ${ in #: line\n");
        maybe_token_free(buf);
        exit(EXIT_INVALID_HEADER);
    }
    char *name = strndup_safe(name_start, (size_t)(*cursor - name_start));
    (*cursor)++;  // Skip }.

    // Check for ${NAME-VALUE} (dash) default syntax.
    char *dash = strchr(name, '-');
    if (dash != NULL && is_identifier(name, (size_t)(dash - name))) {
        *dash = '\0';
        const char *value = getenv(name);
        if (value == NULL) {
            value = dash + 1;
            if (setenv(name, value, 1) != 0) {
                perror("herescript: setenv");
                free(name);
                maybe_token_free(buf);
                exit(EXIT_GENERAL_ERROR);
            }
        }
        maybe_token_is_token(buf);
        for (const char *v = value; *v; v++) {
            maybe_token_append(buf, *v);
        }
        free(name);
        return;
    }

    // Check for ${NAME:=VALUE} (colon-equals) default syntax.
    char *colon_eq = strstr(name, ":=");
    if (colon_eq != NULL && is_identifier(name, (size_t)(colon_eq - name))) {
        *colon_eq = '\0';
        const char *value = getenv(name);
        if (value == NULL) {
            value = colon_eq + 2;
            if (setenv(name, value, 1) != 0) {
                perror("herescript: setenv");
                free(name);
                maybe_token_free(buf);
                exit(EXIT_GENERAL_ERROR);
            }
        }
        maybe_token_is_token(buf);
        for (const char *v = value; *v; v++) {
            maybe_token_append(buf, *v);
        }
        free(name);
        return;
    }

    // Fall through to slice or scalar.
    char const * colon = strchr(name, ':');
    if (colon != NULL) {
        expand_slice_notation(rs, buf, name);
    } else {
        expand_scalar_name(rs, buf, name);
    }
    free(name);
}



// Parse a "..." double-quoted span. Called after consuming the opening quote.
// Supports ${...} variable interpolation and minimal backslash escapes: \\ \" \$.
// Also supports bare $NAME and $<digits> using greedy bash-style matching.
// All other backslash sequences are passed through literally.
// Advances *cursor past the closing double quote.

// Expand a bare $NAME or $<digits> substitution using greedy, bash-style
// matching. Called after consuming the '$'; *cursor points at the first
// character of the name. Advances *cursor past the consumed name.
static void expand_dollar_bare(RunState *rs, MaybeToken *buf, const char **cursor) {
    const char *name_start = *cursor;
    if (isdigit((unsigned char)**cursor)) {
        // Greedy digit run: $1, $10, $123, etc.
        while (isdigit((unsigned char)**cursor)) {
            (*cursor)++;
        }
    } else {
        // Greedy identifier: starts with letter or underscore, continues with
        // alphanumerics or underscores.
        (*cursor)++;  // The first character has already been validated by the caller.
        while (is_name_char(**cursor)) {
            (*cursor)++;
        }
    }
    char *name = strndup_safe(name_start, (size_t)(*cursor - name_start));
    expand_scalar_name(rs, buf, name);
    free(name);
}


// Parse a "..." double-quoted span. Called after consuming the opening quote.
// Supports ${...} variable interpolation and minimal backslash escapes: \\ \" \$.
// All other backslash sequences are passed through literally.
// Advances *cursor past the closing double quote.
// Precondition: *cursor is non-NULL and points into a NUL-terminated string.
static void scan_double_quote(RunState *rs, MaybeToken *buf, const char **cursor) {
    maybe_token_is_token(buf);  // Empty double quotes still produce a token.
    while (**cursor && **cursor != '"') {
        // Peek one character ahead. This is safe because **cursor is non-NUL,
        // so (*cursor)+1 is at worst the NUL terminator of the string.
        const char ch = *((*cursor) + 1);
        if (**cursor == '$' && ch == '{') {
            (*cursor) += 2;
            expand_dollar_brace(rs, buf, cursor);
        } else if (**cursor == '$' && is_name_char(*((*cursor) + 1))) {
            // Bare $NAME or $<digits> — greedy bash-style expansion.
            (*cursor)++;
            expand_dollar_bare(rs, buf, cursor);
        } else if (**cursor == '\\' && (ch == '\\' || ch == '"' || ch == '$')) {
            (*cursor)++;  // Skip backslash.
            maybe_token_append(buf, *(*cursor)++);
        } else {
            maybe_token_append(buf, *(*cursor)++);
        }
    }
    if (**cursor == '"') {
        (*cursor)++;  // Skip closing quote.
    } else {
        fprintf(stderr, "herescript: unterminated double quote in #: line\n");
        maybe_token_free(buf);
        exit(EXIT_INVALID_HEADER);
    }
}

// Return true when cursor points to a shell-style binding: an identifier
// (letter or underscore, followed by alphanumerics/underscores) then either
// '=' (unconditional) or ':=' (conditional). On match, *sep_out points at the
// '=' or ':', and *conditional is set accordingly.
static bool is_binding_start(const char *cursor, const char **sep_out, bool *conditional) {
    if (!is_first_name_char(*cursor)) return false;
    const char *p = cursor + 1;
    while (is_name_char(*p)) {
        p++;
    }
    if (*p == '=') {
        *sep_out = p;
        *conditional = false;
        return true;
    }
    if (*p == ':' && *(p + 1) == '=') {
        *sep_out = p;
        *conditional = true;
        return true;
    }
    return false;
}

// Process a #: arguments line using shell-like tokenisation. Tokens are
// separated by whitespace; quoting and substitution forms are dispatched to
// dedicated helpers.
//
// Leading tokens of the form NAME=VALUE are treated as environment variable
// bindings (like the shell). Once a non-binding token is seen, all subsequent
// tokens are arguments. Quoting the name or '=' prevents binding recognition,
// providing an escape mechanism.
static void run_state_process_colon_line(RunState *rs, const char *line) {
    const char *cursor = line + 2;  // Skip the "#:" prefix.
    MaybeToken buf;
    maybe_token_init(&buf, 64);
    bool binding_prefix = true;  // Still in the leading binding portion of the line.

    while (*cursor) {
        // Skip inter-token whitespace.
        while (*cursor && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (!*cursor) break;

        // While still in the binding prefix, check for NAME=... or NAME:=... syntax.
        char *binding_name = NULL;
        bool binding_conditional = false;
        if (binding_prefix) {
            const char *sep;
            if (is_binding_start(cursor, &sep, &binding_conditional)) {
                binding_name = strndup_safe(cursor, (size_t)(sep - cursor));
                // Advance past '=' or ':='.
                cursor = sep + (binding_conditional ? 2 : 1);
            }
        }
        if (!binding_name) {
            binding_prefix = false;
        }

        // Accumulate one token, dispatching on the leading character(s).
        while (*cursor && !isspace((unsigned char)*cursor)) {
            if (*cursor == '$' && *(cursor + 1) == '@') {
                cursor += 2;
                expand_slice(rs, &buf, 1, rs->user_param_count + 1);
            } else if (*cursor == '$' && *(cursor + 1) == '\'') {
                cursor += 2;
                scan_dollar_single_quote(&buf, &cursor);
            } else if (*cursor == '$' && *(cursor + 1) == '"') {
                cursor += 2;
                scan_dollar_double_quote(&buf, &cursor);
            } else if (*cursor == '$' && *(cursor + 1) == '{') {
                cursor += 2;
                expand_dollar_brace(rs, &buf, &cursor);
            } else if (*cursor == '$' && is_name_char(*(cursor + 1))) {
                // Bare $NAME or $<digits> — greedy bash-style expansion.
                cursor++;
                expand_dollar_bare(rs, &buf, &cursor);
            } else if (*cursor == '\'') {
                cursor++;
                scan_single_quote(&buf, &cursor);
            } else if (*cursor == '"') {
                cursor++;
                scan_double_quote(rs, &buf, &cursor);
            } else {
                maybe_token_append(&buf, *cursor++);
            }
        }

        if (binding_name) {
            // Collect the value and bind it as an environment variable.
            // Conditional bindings (NAME:=VALUE) only take effect when NAME is unset.
            char *value;
            if (buf.is_token) {
                value = maybe_token_take(&buf);
            } else {
                value = strdup_safe("");
            }
            bool should_set = !binding_conditional || getenv(binding_name) == NULL;
            if (should_set && setenv(binding_name, value, 1) != 0) {
                perror("herescript: setenv");
                maybe_token_free(&buf);
                free(binding_name);
                free(value);
                exit(EXIT_GENERAL_ERROR);
            }
            free(binding_name);
            free(value);
        } else {
            // Flush any remaining content as a token. Slice expansions flush the
            // buffer themselves, so a standalone slice leaves nothing to flush here.
            run_state_flush_maybe_token(rs, &buf);
        }
    }

    maybe_token_free(&buf);
}

// Open path, read every line, and process those that begin with "#:" exactly
// as if they had appeared as header lines in the script itself. All other lines
// (including blank lines, shell-comment lines, and shebang lines) are silently
// skipped, so a herescript file can serve as its own load-file source.
static int run_state_load_file(RunState *rs, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "herescript: --load-file: ");
        perror(path);
        return EXIT_GENERAL_ERROR;
    }
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    while ((line_len = getline(&line, &line_cap, fp)) >= 0) {
        if (line_len >= 2 && line[0] == '#' && line[1] == ':') {
            run_state_process_colon_line(rs, line);
        }
    }
    free(line);
    fclose(fp);
    return 0;
}

// Resolve DIR to an absolute path (must exist and be a directory) and prepend
// it to the PATH environment variable. When PATH is unset or empty, PATH is
// simply set to the resolved directory.
static int run_state_path_prepend(RunState const *rs, const char *dir) {
    (void)rs;
    struct stat st;
    if (stat(dir, &st) != 0) {
        fprintf(stderr, "herescript: --path-prepend: ");
        perror(dir);
        return EXIT_GENERAL_ERROR;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "herescript: --path-prepend: %s: not a directory\n", dir);
        return EXIT_GENERAL_ERROR;
    }
    char *abs = realpath(dir, NULL);
    if (!abs) {
        fprintf(stderr, "herescript: --path-prepend: ");
        perror(dir);
        return EXIT_GENERAL_ERROR;
    }
    const char *current = getenv("PATH");
    char *new_path;
    if (current && *current) {
        size_t len = strlen(abs) + 1 + strlen(current) + 1;
        new_path = malloc(len);
        if (!new_path) {
            perror("malloc");
            free(abs);
            exit(EXIT_GENERAL_ERROR);
        }
        snprintf(new_path, len, "%s:%s", abs, current);
    } else {
        new_path = strdup_safe(abs);
    }
    int rc = 0;
    if (setenv("PATH", new_path, 1) != 0) {
        perror("herescript: setenv PATH");
        rc = EXIT_GENERAL_ERROR;
    }
    free(abs);
    free(new_path);
    return rc;
}

// Process a #! options line. Tokens are extracted by simple whitespace splitting.
// Supports: --chdir DIRECTORY        or  --chdir=DIRECTORY
//           --load-file FILE         or  --load-file=FILE
//           --path-prepend DIRECTORY or  --path-prepend=DIRECTORY
//           --umask MASK             or  --umask=MASK
//           --dry-run
static int run_state_process_bang_line(RunState *rs, const char *line) {
    const char *cursor = line + 2;  // Skip "#!" prefix.

    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) break;

        // Collect one token.
        const char *tok_start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
        size_t tok_len = (size_t)(cursor - tok_start);

        // Match known options.
        if (tok_len == 9 && strncmp(tok_start, "--dry-run", 9) == 0) {
            rs->dry_run = true;
        } else if (tok_len >= 11 && strncmp(tok_start, "--load-file", 11) == 0) {
            char *file_path;
            if (tok_len > 11 && tok_start[11] == '=') {
                // --load-file=FILE form.
                file_path = strndup_safe(tok_start + 12, tok_len - 12);
            } else if (tok_len == 11) {
                // --load-file FILE form: consume next whitespace-delimited token.
                while (*cursor && isspace((unsigned char)*cursor)) cursor++;
                if (!*cursor) {
                    fprintf(stderr, "herescript: --load-file requires a file argument\n");
                    return EXIT_INVALID_HEADER;
                }
                const char *val_start = cursor;
                while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
                file_path = strndup_safe(val_start, (size_t)(cursor - val_start));
            } else {
                fprintf(stderr, "herescript: unrecognised option: %.*s\n", (int)tok_len, tok_start);
                return EXIT_INVALID_HEADER;
            }
            int rc = run_state_load_file(rs, file_path);
            free(file_path);
            if (rc != 0) return rc;
        } else if (tok_len >= 7 && strncmp(tok_start, "--umask", 7) == 0) {
            char *mask_str;
            if (tok_len > 7 && tok_start[7] == '=') {
                // --umask=MASK form.
                mask_str = strndup_safe(tok_start + 8, tok_len - 8);
            } else if (tok_len == 7) {
                // --umask MASK form: consume next whitespace-delimited token.
                while (*cursor && isspace((unsigned char)*cursor)) cursor++;
                if (!*cursor) {
                    fprintf(stderr, "herescript: --umask requires a mask argument\n");
                    return EXIT_INVALID_HEADER;
                }
                const char *val_start = cursor;
                while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
                mask_str = strndup_safe(val_start, (size_t)(cursor - val_start));
            } else {
                fprintf(stderr, "herescript: unrecognised option: %.*s\n", (int)tok_len, tok_start);
                return EXIT_INVALID_HEADER;
            }
            char *end;
            long val = strtol(mask_str, &end, 8);
            if (end == mask_str || *end != '\0' || val < 0 || val > 0777) {
                fprintf(stderr, "herescript: --umask: invalid mask '%s' (expected octal 0-0777)\n", mask_str);
                free(mask_str);
                return EXIT_INVALID_HEADER;
            }
            rs->umask_value = (mode_t)val;
            rs->umask_set = true;
            free(mask_str);
        } else if (tok_len >= 14 && strncmp(tok_start, "--path-prepend", 14) == 0) {
            char *dir;
            if (tok_len > 14 && tok_start[14] == '=') {
                // --path-prepend=DIRECTORY form.
                dir = strndup_safe(tok_start + 15, tok_len - 15);
            } else if (tok_len == 14) {
                // --path-prepend DIRECTORY form: consume next whitespace-delimited token.
                while (*cursor && isspace((unsigned char)*cursor)) cursor++;
                if (!*cursor) {
                    fprintf(stderr, "herescript: --path-prepend requires a directory argument\n");
                    return EXIT_INVALID_HEADER;
                }
                const char *val_start = cursor;
                while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
                dir = strndup_safe(val_start, (size_t)(cursor - val_start));
            } else {
                fprintf(stderr, "herescript: unrecognised option: %.*s\n", (int)tok_len, tok_start);
                return EXIT_INVALID_HEADER;
            }
            int rc = run_state_path_prepend(rs, dir);
            free(dir);
            if (rc != 0) return rc;
        } else if (tok_len >= 7 && strncmp(tok_start, "--chdir", 7) == 0) {
            if (tok_len > 7 && tok_start[7] == '=') {
                // --chdir=DIRECTORY form.
                free(rs->chdir_target);
                rs->chdir_target = strndup_safe(tok_start + 8, tok_len - 8);
            } else if (tok_len == 7) {
                // --chdir DIRECTORY form: consume next whitespace-delimited token.
                while (*cursor && isspace((unsigned char)*cursor)) cursor++;
                if (!*cursor) {
                    fprintf(stderr, "herescript: --chdir requires a directory argument\n");
                    return EXIT_INVALID_HEADER;
                }
                const char *val_start = cursor;
                while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
                free(rs->chdir_target);
                rs->chdir_target = strndup_safe(val_start, (size_t)(cursor - val_start));
            } else {
                fprintf(stderr, "herescript: unrecognised option: %.*s\n", (int)tok_len, tok_start);
                return EXIT_INVALID_HEADER;
            }
        } else if (tok_len >= 7 && strncmp(tok_start, "--unset", 7) == 0) {
            char *var_name;
            if (tok_len > 7 && tok_start[7] == '=') {
                // --unset=VAR form.
                var_name = strndup_safe(tok_start + 8, tok_len - 8);
            } else if (tok_len == 7) {
                // --unset VAR form: consume next whitespace-delimited token.
                while (*cursor && isspace((unsigned char)*cursor)) cursor++;
                if (!*cursor) {
                    fprintf(stderr, "herescript: --unset requires a variable name argument\n");
                    return EXIT_INVALID_HEADER;
                }
                const char *val_start = cursor;
                while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
                var_name = strndup_safe(val_start, (size_t)(cursor - val_start));
            } else {
                fprintf(stderr, "herescript: unrecognised option: %.*s\n", (int)tok_len, tok_start);
                return EXIT_INVALID_HEADER;
            }
            append_string_array(&rs->unset_vars, var_name);
        } else {
            fprintf(stderr, "herescript: unrecognised option: %.*s\n", (int)tok_len, tok_start);
            return EXIT_INVALID_HEADER;
        }
    }
    return 0;
}

// ============================================================================
// Main Program
// ============================================================================

#ifndef HERESCRIPT_UNIT_TEST
int main(int argc, char **argv) {
    // Handle --help before the argc check so that `herescript --help` works.
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf(
            "Usage: herescript <executable> <script> [args...]\n"
            "\n"
            "Herescript is a modern, structured interpreter launcher designed to extend\n"
            "the limited Unix shebang mechanism. Scripts begin with\n"
            "'#!/usr/local/bin/herescript EXECUTABLE' and use header lines to build the argument\n"
            "list. For example:\n"
            "\n"
            "    #!/usr/local/bin/herescript python3\n"
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

    if (argc < 3) {
        fprintf(stderr, "herescript: no script specified\n");
        fprintf(stderr, "  Hint: This program is meant to be used as a shebang interpreter.\n");
        return EXIT_GENERAL_ERROR;
    }

    // Initialize run state.
    RunState rs;
    run_state_init(&rs);

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

    run_state_bind_herescript_file(&rs);

    // Store user-supplied arguments so that ${N} (Step 2e) can expand them.
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
    
    if (line_len > MAX_LINE_LENGTH) {
        fprintf(stderr, "herescript: shebang line too long (%zd bytes, limit %d)\n",
                line_len, MAX_LINE_LENGTH);
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }

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
        fprintf(stderr, "  Hint: The shebang must specify an executable after #!/usr/local/bin/herescript.\n");
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    // Check for options (space in executable).
    if (strchr(exec_part, ' ') || strchr(exec_part, '\t')) {
        fprintf(stderr, "herescript: shebang contains options, which are not allowed\n");
        fprintf(stderr, "  Expected: #!/usr/local/bin/herescript <executable>\n");
        fprintf(stderr, "  Got: %s\n", line);
        fprintf(stderr, "  Hint: Options to the executable should be specified in header lines,\n");
        fprintf(stderr, "        not in the shebang line.\n");
        fclose(fp);
        free(line);
        return EXIT_MALFORMED_SHEBANG;
    }
    
    rs.executable = strdup_safe(exec_part);
    run_state_bind_herescript_command(&rs);
    free(line);
    line = NULL;
    
    // Parse header lines. Trailing newlines are not stripped here; the header
    // type checks use line[0]/line[1] directly, and process_colon_line's
    // whitespace tokeniser discards any trailing newline as inter-token space.
    //
    // inline_buf accumulates text from consecutive #> lines. When a non-#>
    // header line (or end-of-headers) is encountered, any accumulated text is
    // flushed as a HERESCRIPT<N> environment variable.
    MaybeToken inline_buf;
    maybe_token_init(&inline_buf, 256);

    while ((line_len = getline(&line, &line_cap, fp)) >= 0) {
        // Check if this is a header line? If not, we're done parsing headers and can move on to execution. 
        if (line_len < 2 || line[0] != '#') break;

        // A non-#> header line ends any in-progress inline argument.
        if (line[1] != '>' && inline_buf.is_token) {
            char *value = maybe_token_take(&inline_buf);
            char env_name[32];
            snprintf(env_name, sizeof(env_name), "HERESCRIPT%d", rs.inline_arg_count++);
            if (setenv(env_name, value, 1) != 0) {
                perror("herescript: setenv");
                free(value);
                maybe_token_free(&inline_buf);
                fclose(fp);
                free(line);
                return EXIT_GENERAL_ERROR;
            }
            free(value);
        }

        switch (line[1]) {
            case '#':
                // Comment line, skip.
                continue;
            case ':':
                run_state_process_colon_line(&rs, line);
                break;
            case '>':
                // Inline quoted argument. The third character must be a space;
                // content starts at offset 3. Consecutive #> lines are glued
                // together preserving the line breaks between them.
                if (line_len >= 3 && line[2] == ' ') {
                    if (inline_buf.is_token) {
                        // Glue to preceding #> line with a newline separator.
                        maybe_token_append(&inline_buf, '\n');
                    }
                    // Append everything after the "#> " prefix, stripping the
                    // trailing newline if present.
                    const char *content = line + 3;
                    size_t content_len = (size_t)line_len - 3;
                    if (content_len > 0 && content[content_len - 1] == '\n') {
                        content_len--;
                    }
                    for (size_t i = 0; i < content_len; i++) {
                        maybe_token_append(&inline_buf, content[i]);
                    }
                    // Mark as a token even if the content is empty (e.g. "#> ").
                    maybe_token_is_token(&inline_buf);
                } else {
                    fprintf(stderr, "herescript: malformed #> line (expected '#> ' prefix)\n");
                    maybe_token_free(&inline_buf);
                    fclose(fp);
                    free(line);
                    return EXIT_INVALID_HEADER;
                }
                break;
            case '!': {
                int bang_rc = run_state_process_bang_line(&rs, line);
                if (bang_rc != 0) {
                    maybe_token_free(&inline_buf);
                    fclose(fp);
                    free(line);
                    return bang_rc;
                }
                break;
            }
            default:
                // Any unrecognised #X line (e.g. "# comment") ends the header block.
                goto done_headers;
        }
    }
    done_headers:
    // Flush any trailing inline argument that was not terminated by a
    // non-#> header line.
    if (inline_buf.is_token) {
        char *value = maybe_token_take(&inline_buf);
        char env_name[32];
        snprintf(env_name, sizeof(env_name), "HERESCRIPT%d", rs.inline_arg_count++);
        if (setenv(env_name, value, 1) != 0) {
            perror("herescript: setenv");
            free(value);
            maybe_token_free(&inline_buf);
            fclose(fp);
            free(line);
            return EXIT_GENERAL_ERROR;
        }
        free(value);
    }
    maybe_token_free(&inline_buf);
    
    free(line);
    fclose(fp);

    // All memory allocated during this process — arguments, bindings,
    // script_path, executable — is intentionally left unfreed. On a
    // successful exec the kernel replaces the process image entirely, so the
    // allocations simply cease to exist. On an error return main() exits
    // immediately afterward and the OS reclaims everything. There is no code
    // path in which this process continues running after this point, so
    // explicit teardown would be pure ceremony with no practical effect.
    return run_state_exec(&rs);
}
#endif // HERESCRIPT_UNIT_TEST
