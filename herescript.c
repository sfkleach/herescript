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

// Environment binding.
typedef struct {
    char *name;
    char *value;
    bool conditional;  // true for :=, false for =
} Binding;

// Dynamic array for bindings.
typedef struct {
    Binding *items;
    size_t count;
    size_t capacity;
} BindingArray;

// Metacharacters flags.
typedef struct {
    bool literal;          // !
    bool no_escape;        // backslash
    bool no_subst;         // $
    bool no_binding;       // =
    bool comment;          // #
} Metachars;

// Global state.
static StringArray arguments;
static BindingArray bindings;
static bool script_name_used = false;
static char *script_path = NULL;
static char *executable = NULL;

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

static void init_binding_array(BindingArray *arr, size_t initial_capacity) {
    arr->items = malloc(initial_capacity * sizeof(Binding));
    if (!arr->items) {
        perror("malloc");
        exit(EXIT_GENERAL_ERROR);
    }
    arr->count = 0;
    arr->capacity = initial_capacity;
}

static void append_binding_array(BindingArray *arr, Binding binding) {
    if (arr->count >= arr->capacity) {
        arr->capacity *= 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(Binding));
        if (!arr->items) {
            perror("realloc");
            exit(EXIT_GENERAL_ERROR);
        }
    }
    arr->items[arr->count++] = binding;
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

static void strip_whitespace(char *str) {
    // Strip leading whitespace.
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    
    // Strip trailing whitespace.
    char *end = start + strlen(start) - 1;
    while (end >= start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
    
    // Move stripped content to beginning if needed.
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
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
// Escape Processing
// ============================================================================

static char *process_escapes(const char *input) {
    size_t len = strlen(input);
    char *output = malloc(len + 1);  // At most same length.
    if (!output) {
        perror("malloc");
        exit(EXIT_GENERAL_ERROR);
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\\' && i + 1 < len) {
            char next = input[i + 1];
            switch (next) {
                case '\\': output[j++] = '\\'; i++; break;
                case 'n':  output[j++] = '\n'; i++; break;
                case 'r':  output[j++] = '\r'; i++; break;
                case 't':  output[j++] = '\t'; i++; break;
                case 's':  output[j++] = ' ';  i++; break;
                case '$':  output[j++] = '$';  i++; break;
                default:
                    // Unrecognised escape becomes literal.
                    output[j++] = input[i];
                    break;
            }
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
    return output;
}

// ============================================================================
// Substitution Processing
// ============================================================================

static char *process_substitution(const char *input) {
    size_t len = strlen(input);
    size_t output_size = len * 2;  // Start with double size.
    char *output = malloc(output_size);
    if (!output) {
        perror("malloc");
        exit(EXIT_GENERAL_ERROR);
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '$' && i + 1 < len && input[i + 1] == '{') {
            // Find closing brace.
            size_t start = i + 2;
            size_t end = start;
            while (end < len && input[end] != '}') {
                end++;
            }
            
            if (end >= len) {
                // No closing brace - treat as literal.
                output[j++] = input[i];
                continue;
            }
            
            // Extract name.
            size_t name_len = end - start;
            char *name = malloc(name_len + 1);
            if (!name) {
                perror("malloc");
                exit(EXIT_GENERAL_ERROR);
            }
            memcpy(name, input + start, name_len);
            name[name_len] = '\0';
            
            const char *value;
            if (name_len == 0) {
                // ${} expands to script filename.
                // printf("Debug: Substituting ${} with script path: %s\n", script_path);
                value = script_path;
                script_name_used = true;
            } else {
                value = getenv_or_fail(name);
            }
            free(name);
            
            // Ensure enough space.
            size_t value_len = strlen(value);
            while (j + value_len >= output_size) {
                output_size *= 2;
                output = realloc(output, output_size);
                if (!output) {
                    perror("realloc");
                    exit(EXIT_GENERAL_ERROR);
                }
            }
            
            // Copy value.
            memcpy(output + j, value, value_len);
            j += value_len;
            
            // Skip past the closing brace.
            i = end;
        } else {
            // Ensure enough space.
            if (j >= output_size - 1) {
                output_size *= 2;
                output = realloc(output, output_size);
                if (!output) {
                    perror("realloc");
                    exit(EXIT_GENERAL_ERROR);
                }
            }
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
    return output;
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
        while (*p && !isspace((unsigned char)*p)) {
            if (*p == '$' && *(p + 1) == '\'') {
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

        append_string_array(&arguments, bytebuf_take(&buf));
    }

    bytebuf_free(&buf);
}

// ============================================================================
// Header Line Parsing
// ============================================================================

static void parse_metachars(const char *line, Metachars *meta, size_t *body_start) {
    memset(meta, 0, sizeof(Metachars));
    
    // Skip "#!".
    size_t i = 2;
    
    // Parse metacharacters.
    while (line[i] && !isspace((unsigned char)line[i])) {
        switch (line[i]) {
            case '!': meta->literal = true; break;
            case '\\': meta->no_escape = true; break;
            case '$': meta->no_subst = true; break;
            case '=': meta->no_binding = true; break;
            case '#': meta->comment = true; break;
            default:
                fprintf(stderr, "herescript: invalid metacharacter '%c' in header line\n", line[i]);
                fprintf(stderr, "  Hint: Valid metacharacters are: ! \\ $ = #\n");
                exit(EXIT_INVALID_HEADER);
        }
        i++;
    }
    
    // Skip whitespace.
    while (line[i] && isspace((unsigned char)line[i])) {
        i++;
    }
    
    *body_start = i;
}

static bool is_valid_var_name(const char *name) {
    if (!name || !*name) return false;
    
    // First character must be alpha or underscore.
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') {
        return false;
    }
    
    // Rest must be alnum or underscore.
    for (size_t i = 1; name[i]; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') {
            return false;
        }
    }
    
    return true;
}

static void process_header_line(const char *line) {
    Metachars meta;
    size_t body_start;
    
    // Check if line has no whitespace after metachars (short form).
    size_t i = 2;
    while (line[i] && !isspace((unsigned char)line[i])) {
        i++;
    }
    
    bool has_whitespace = line[i] != '\0';
    
    parse_metachars(line, &meta, &body_start);
    
    // Comment lines are discarded.
    if (meta.comment) {
        return;
    }
    
    // Extract body.
    char *body = strdup_safe(line + body_start);
    strip_whitespace(body);
    
    // Literal mode: everything becomes a literal argument.
    if (meta.literal) {
        if (!has_whitespace) {
            // Short form with ! becomes empty string argument.
            append_string_array(&arguments, strdup_safe(""));
        } else {
            append_string_array(&arguments, body);
        }
        return;
    }
    
    // Apply escape processing unless disabled.
    if (!meta.no_escape) {
        char *processed = process_escapes(body);
        free(body);
        body = processed;
    }
    
    // Apply substitution processing unless disabled.
    if (!meta.no_subst) {
        char *processed = process_substitution(body);
        free(body);
        body = processed;
    }
    
    // Short form without literal or comment becomes empty string argument.
    if (!has_whitespace) {
        append_string_array(&arguments, strdup_safe(""));
        free(body);
        return;
    }
    
    // Classification: binding or argument?
    bool is_binding = false;
    
    if (!meta.no_binding && body[0] != '-') {
        // Check for NAME=VALUE or NAME:=VALUE.
        char *eq = strchr(body, '=');
        if (eq && eq > body) {
            // Check if it's :=.
            bool conditional = false;
            if (eq[-1] == ':') {
                conditional = true;
                eq--;  // Point to the ':'.
            }
            
            // Extract name.
            size_t name_len = eq - body;
            char *name = malloc(name_len + 1);
            if (!name) {
                perror("malloc");
                exit(EXIT_GENERAL_ERROR);
            }
            memcpy(name, body, name_len);
            name[name_len] = '\0';
            
            if (is_valid_var_name(name)) {
                is_binding = true;
                
                // Extract value (after = or :=).
                const char *value_start = eq + (conditional ? 2 : 1);
                char *value = strdup_safe(value_start);
                
                // Apply binding immediately so subsequent substitutions can use it.
                if (conditional) {
                    // Only set if not already set.
                    if (!getenv(name)) {
                        setenv(name, value, 1);
                    }
                } else {
                    // Always set.
                    setenv(name, value, 1);
                }
                
                Binding binding = {
                    .name = name,
                    .value = value,
                    .conditional = conditional
                };
                append_binding_array(&bindings, binding);
            } else {
                free(name);
                // Invalid binding becomes an error when = interpretation is enabled.
                fprintf(stderr, "herescript: invalid variable name in binding: %s\n", body);
                fprintf(stderr, "  Hint: Variable names must start with a letter or underscore,\n");
                fprintf(stderr, "        followed by letters, digits, or underscores.\n");
                exit(EXIT_INVALID_HEADER);
            }
        }
    }
    
    // If not a binding, it's a positional argument.
    if (!is_binding) {
        append_string_array(&arguments, body);
    } else {
        free(body);
    }
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
            "the idiom of starting shebang scripts with #!/usr/bin/env. Instead, scripts begin\n"
            "with '#!/usr/bin/herescript EXECUTABLE' and arguments are specified using\n"
            "continuation lines that each begin with '#!'. For example:\n"
            "\n"
            "    #!/usr/bin/herescript python3\n"
            "    #! --verbose\n"
            "    #! ${}\n"
            "\n"
            "Here '#! --verbose' adds --verbose as an argument, and '#! ${}' controls\n"
            "exactly where the script filename appears in the argument list.\n"
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
    init_binding_array(&bindings, 64);

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
        
        if (line[1] != '!') {
            break;  // End of header block.
        }
        
        process_header_line(line);
    }
    
    free(line);
    fclose(fp);
    
    // Add any command-line arguments passed to herescript (after the script name).
    for (int i = 3; i < argc; i++) {
        append_string_array(&arguments, strdup_safe(argv[i]));
    }
    
    // Append script filename if ${} was not used.
    if (!script_name_used) {
        // printf("Debug: Appending script path to arguments since ${} was not used.\n");
        append_string_array(&arguments, strdup_safe(script_path));
    }
    
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
