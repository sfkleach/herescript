/*
 * test-herescript.c
 *
 * A test utility that outputs JSON containing the environment variables
 * and command-line arguments it receives. Used for testing herescript behavior.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// External environment variables provided by the system.
extern char **environ;

/*
 * Escape a string for JSON output.
 * Handles: " \ / \b \f \n \r \t and control characters.
 * Returns a newly allocated string that must be freed by the caller.
 */
static char *json_escape(const char *str) {
    if (str == NULL) {
        return strdup("null");
    }

    // Calculate required size (worst case: every char needs escaping).
    size_t len = strlen(str);
    size_t max_size = len * 6 + 1;  // \uXXXX is worst case (6 chars per char).
    char *escaped = malloc(max_size);
    if (escaped == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    char *out = escaped;
    for (const char *in = str; *in; in++) {
        switch (*in) {
            case '"':
                *out++ = '\\';
                *out++ = '"';
                break;
            case '\\':
                *out++ = '\\';
                *out++ = '\\';
                break;
            case '\b':
                *out++ = '\\';
                *out++ = 'b';
                break;
            case '\f':
                *out++ = '\\';
                *out++ = 'f';
                break;
            case '\n':
                *out++ = '\\';
                *out++ = 'n';
                break;
            case '\r':
                *out++ = '\\';
                *out++ = 'r';
                break;
            case '\t':
                *out++ = '\\';
                *out++ = 't';
                break;
            default:
                if ((unsigned char)*in < 0x20) {
                    // Control character: use \uXXXX format.
                    out += sprintf(out, "\\u%04x", (unsigned char)*in);
                } else {
                    *out++ = *in;
                }
                break;
        }
    }
    *out = '\0';
    return escaped;
}

/*
 * Print a JSON string array.
 */
static void print_string_array(const char *label, char **strings, int count) {
    printf("    \"%s\": [\n", label);
    for (int i = 0; i < count; i++) {
        char *escaped = json_escape(strings[i]);
        printf("        \"%s\"", escaped);
        if (i < count - 1) {
            printf(",");
        }
        printf("\n");
        free(escaped);
    }
    printf("    ]");
}

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int main(int argc, char *argv[]) {
    // Count environment variables.
    int env_count = 0;
    for (char **env = environ; *env != NULL; env++) {
        env_count++;
    }

    // Sort environment variables for deterministic output.
    char **sorted_env = malloc((size_t)env_count * sizeof(char *));
    if (!sorted_env) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    for (int i = 0; i < env_count; i++) {
        sorted_env[i] = environ[i];
    }
    qsort(sorted_env, (size_t)env_count, sizeof(char *), compare_strings);

    // Output JSON.
    printf("{\n");

    // Print environment variables.
    print_string_array("env", sorted_env, env_count);
    printf(",\n");

    // Print arguments.
    print_string_array("argv", argv, argc);

    // Optionally print cwd when HERESCRIPT_SHOW_CWD is set.
    if (getenv("HERESCRIPT_SHOW_CWD") != NULL) {
        char cwd_buf[4096];
        const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf));
        char *escaped = json_escape(cwd ? cwd_buf : NULL);
        printf(",\n    \"cwd\": \"%s\"", escaped);
        free(escaped);
    }

    // Optionally print file creation mask when HERESCRIPT_SHOW_UMASK is set.
    // umask(2) has no read-only form, so we set and restore to peek at it.
    if (getenv("HERESCRIPT_SHOW_UMASK") != NULL) {
        mode_t current = umask(0);
        umask(current);
        printf(",\n    \"umask\": \"%04o\"", (unsigned int)current);
    }

    printf("\n");
    printf("}\n");

    free(sorted_env);
    return 0;
}
