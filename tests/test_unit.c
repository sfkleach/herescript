/*
 * tests/test_unit.c
 *
 * Unity-based unit tests for the static helper functions in herescript.c.
 * The production main() is excluded via HERESCRIPT_UNIT_TEST so that we can
 * include the whole translation unit directly and call static functions.
 * HERESCRIPT_UNIT_TEST is defined on the compiler command line.
 */

#include "../herescript.c"

#include "unity/unity.h"

// ============================================================================
// Unity setUp / tearDown (required by Unity even if empty)
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// MaybeToken tests
// ============================================================================

void test_maybe_token_init_is_not_token(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    TEST_ASSERT_FALSE(b.is_token);
    maybe_token_free(&b);
}

void test_maybe_token_append_sets_is_token(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    maybe_token_append(&b, 'x');
    TEST_ASSERT_TRUE(b.is_token);
    maybe_token_free(&b);
}

void test_maybe_token_take_returns_correct_string(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    maybe_token_append(&b, 'h');
    maybe_token_append(&b, 'i');
    char *result = maybe_token_take(&b);
    TEST_ASSERT_EQUAL_STRING("hi", result);
    free(result);
    maybe_token_free(&b);
}

void test_maybe_token_take_resets_builder(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    maybe_token_append(&b, 'x');
    char *result = maybe_token_take(&b);
    free(result);
    TEST_ASSERT_FALSE(b.is_token);
    TEST_ASSERT_EQUAL_size_t(0, b.len);
    maybe_token_free(&b);
}

void test_maybe_token_is_token_marks_empty_as_token(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    TEST_ASSERT_FALSE(b.is_token);
    maybe_token_is_token(&b);
    TEST_ASSERT_TRUE(b.is_token);
    maybe_token_free(&b);
}

void test_maybe_token_take_empty_token_is_empty_string(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    maybe_token_is_token(&b);
    char *result = maybe_token_take(&b);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
    maybe_token_free(&b);
}

// ============================================================================
// maybe_token_append_escape tests
// ============================================================================

// Helper: append an escape and return the resulting string. Caller must free.
static char *escape_result(char c) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    maybe_token_append_escape(&b, c);
    return maybe_token_take(&b);
}

void test_escape_backslash(void) {
    char *r = escape_result('\\');
    TEST_ASSERT_EQUAL_STRING("\\", r);
    free(r);
}

void test_escape_single_quote(void) {
    char *r = escape_result('\'');
    TEST_ASSERT_EQUAL_STRING("'", r);
    free(r);
}

void test_escape_double_quote(void) {
    char *r = escape_result('"');
    TEST_ASSERT_EQUAL_STRING("\"", r);
    free(r);
}

void test_escape_s_is_space(void) {
    char *r = escape_result('s');
    TEST_ASSERT_EQUAL_STRING(" ", r);
    free(r);
}

void test_escape_n_is_newline(void) {
    char *r = escape_result('n');
    TEST_ASSERT_EQUAL_STRING("\n", r);
    free(r);
}

void test_escape_t_is_tab(void) {
    char *r = escape_result('t');
    TEST_ASSERT_EQUAL_STRING("\t", r);
    free(r);
}

void test_escape_r_is_carriage_return(void) {
    char *r = escape_result('r');
    TEST_ASSERT_EQUAL_STRING("\r", r);
    free(r);
}

void test_escape_unknown_emits_backslash_and_char(void) {
    char *r = escape_result('z');
    TEST_ASSERT_EQUAL_STRING("\\z", r);
    free(r);
}

void test_escape_unknown_sets_is_token(void) {
    MaybeToken b;
    maybe_token_init(&b, 16);
    maybe_token_append_escape(&b, 'q');
    TEST_ASSERT_TRUE(b.is_token);
    char *r = maybe_token_take(&b);
    free(r);
    maybe_token_free(&b);
}

// ============================================================================
// expand_scalar_name tests
// ============================================================================

// Build a minimal RunState with a known set of user params.
static RunState make_run_state(const char *script_path, const char **user_params, int count) {
    RunState rs;
    run_state_init(&rs);
    rs.script_path = (char *)script_path;
    rs.user_params = (char **)user_params;
    rs.user_param_count = count;
    return rs;
}

void test_expand_scalar_name_param_zero_is_script_path(void) {
    const char *params[] = {"a", "b"};
    RunState rs = make_run_state("/my/script.sh", params, 2);
    MaybeToken buf;
    maybe_token_init(&buf, 32);

    expand_scalar_name(&rs, &buf, "0");

    char *result = maybe_token_take(&buf);
    TEST_ASSERT_EQUAL_STRING("/my/script.sh", result);
    free(result);
    maybe_token_free(&buf);
    // StringArray was initialised by run_state_init; free its backing store.
    free(rs.arguments.items);
}

void test_expand_scalar_name_param_one(void) {
    const char *params[] = {"first", "second"};
    RunState rs = make_run_state("/s", params, 2);
    MaybeToken buf;
    maybe_token_init(&buf, 32);

    expand_scalar_name(&rs, &buf, "1");

    char *result = maybe_token_take(&buf);
    TEST_ASSERT_EQUAL_STRING("first", result);
    free(result);
    maybe_token_free(&buf);
    free(rs.arguments.items);
}

void test_expand_scalar_name_param_out_of_range_is_empty(void) {
    const char *params[] = {"only"};
    RunState rs = make_run_state("/s", params, 1);
    MaybeToken buf;
    maybe_token_init(&buf, 32);

    expand_scalar_name(&rs, &buf, "99");

    char *result = maybe_token_take(&buf);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
    maybe_token_free(&buf);
    free(rs.arguments.items);
}

void test_expand_scalar_name_env_var(void) {
    setenv("TEST_HERESCRIPT_VAR", "hello_env", 1);
    const char *params[] = {};
    RunState rs = make_run_state("/s", params, 0);
    MaybeToken buf;
    maybe_token_init(&buf, 32);

    expand_scalar_name(&rs, &buf, "TEST_HERESCRIPT_VAR");

    char *result = maybe_token_take(&buf);
    TEST_ASSERT_EQUAL_STRING("hello_env", result);
    free(result);
    maybe_token_free(&buf);
    free(rs.arguments.items);
    unsetenv("TEST_HERESCRIPT_VAR");
}

// ============================================================================
// expand_slice_notation tests (happy path; invalid inputs call exit() so
// those are exercised by the functional test suite instead)
// ============================================================================

// Helper: run expand_slice_notation and return a NULL-terminated StringArray
// of the arguments that were appended to rs. Caller must free the RunState's
// arguments.items and each strdup'd string.
void test_expand_slice_defaults_to_all_params(void) {
    const char *params[] = {"a", "b", "c"};
    RunState rs = make_run_state("/script", params, 3);
    MaybeToken buf;
    maybe_token_init(&buf, 16);

    // `expand_slice_notation` mutates `name` (it writes '\0' at the colon),
    // so it must be a writable buffer.
    char name[] = ":";
    expand_slice_notation(&rs, &buf, name);

    // Arguments should be: ["/script", "a", "b", "c"] (total_argc = 4; slice 0..4).
    TEST_ASSERT_EQUAL_size_t(4, rs.arguments.count);
    TEST_ASSERT_EQUAL_STRING("/script", rs.arguments.items[0]);
    TEST_ASSERT_EQUAL_STRING("a", rs.arguments.items[1]);
    TEST_ASSERT_EQUAL_STRING("b", rs.arguments.items[2]);
    TEST_ASSERT_EQUAL_STRING("c", rs.arguments.items[3]);

    for (size_t i = 0; i < rs.arguments.count; i++) free(rs.arguments.items[i]);
    free(rs.arguments.items);
    maybe_token_free(&buf);
}

void test_expand_slice_lhs_only(void) {
    const char *params[] = {"x", "y", "z"};
    RunState rs = make_run_state("/s", params, 3);
    MaybeToken buf;
    maybe_token_init(&buf, 16);

    char name[] = "2:";  // From index 2 to end (total_argc = 4): params "y", "z".
    expand_slice_notation(&rs, &buf, name);

    TEST_ASSERT_EQUAL_size_t(2, rs.arguments.count);
    TEST_ASSERT_EQUAL_STRING("y", rs.arguments.items[0]);
    TEST_ASSERT_EQUAL_STRING("z", rs.arguments.items[1]);

    for (size_t i = 0; i < rs.arguments.count; i++) free(rs.arguments.items[i]);
    free(rs.arguments.items);
    maybe_token_free(&buf);
}

void test_expand_slice_rhs_only(void) {
    const char *params[] = {"x", "y", "z"};
    RunState rs = make_run_state("/s", params, 3);
    MaybeToken buf;
    maybe_token_init(&buf, 16);

    char name[] = ":2";  // Indices 0..2: script_path and "x".
    expand_slice_notation(&rs, &buf, name);

    TEST_ASSERT_EQUAL_size_t(2, rs.arguments.count);
    TEST_ASSERT_EQUAL_STRING("/s", rs.arguments.items[0]);
    TEST_ASSERT_EQUAL_STRING("x", rs.arguments.items[1]);

    for (size_t i = 0; i < rs.arguments.count; i++) free(rs.arguments.items[i]);
    free(rs.arguments.items);
    maybe_token_free(&buf);
}

void test_expand_slice_both_bounds(void) {
    const char *params[] = {"x", "y", "z"};
    RunState rs = make_run_state("/s", params, 3);
    MaybeToken buf;
    maybe_token_init(&buf, 16);

    char name[] = "1:3";  // Indices 1..3 = params "x", "y".
    expand_slice_notation(&rs, &buf, name);

    TEST_ASSERT_EQUAL_size_t(2, rs.arguments.count);
    TEST_ASSERT_EQUAL_STRING("x", rs.arguments.items[0]);
    TEST_ASSERT_EQUAL_STRING("y", rs.arguments.items[1]);

    for (size_t i = 0; i < rs.arguments.count; i++) free(rs.arguments.items[i]);
    free(rs.arguments.items);
    maybe_token_free(&buf);
}

void test_expand_slice_rhs_clamp(void) {
    const char *params[] = {"a"};
    RunState rs = make_run_state("/s", params, 1);
    MaybeToken buf;
    maybe_token_init(&buf, 16);

    char name[] = ":999";  // RHS beyond total_argc, clamped to 2.
    expand_slice_notation(&rs, &buf, name);

    TEST_ASSERT_EQUAL_size_t(2, rs.arguments.count);

    for (size_t i = 0; i < rs.arguments.count; i++) free(rs.arguments.items[i]);
    free(rs.arguments.items);
    maybe_token_free(&buf);
}

void test_expand_slice_inverted_range_yields_empty(void) {
    const char *params[] = {"a", "b"};
    RunState rs = make_run_state("/s", params, 2);
    MaybeToken buf;
    maybe_token_init(&buf, 16);

    char name[] = "3:1";  // slice_a > slice_b after clamping, snapped to empty.
    expand_slice_notation(&rs, &buf, name);

    TEST_ASSERT_EQUAL_size_t(0, rs.arguments.count);

    free(rs.arguments.items);
    maybe_token_free(&buf);
}

// ============================================================================
// Entry point
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    // MaybeToken lifecycle.
    RUN_TEST(test_maybe_token_init_is_not_token);
    RUN_TEST(test_maybe_token_append_sets_is_token);
    RUN_TEST(test_maybe_token_take_returns_correct_string);
    RUN_TEST(test_maybe_token_take_resets_builder);
    RUN_TEST(test_maybe_token_is_token_marks_empty_as_token);
    RUN_TEST(test_maybe_token_take_empty_token_is_empty_string);

    // Backslash escape handling.
    RUN_TEST(test_escape_backslash);
    RUN_TEST(test_escape_single_quote);
    RUN_TEST(test_escape_double_quote);
    RUN_TEST(test_escape_s_is_space);
    RUN_TEST(test_escape_n_is_newline);
    RUN_TEST(test_escape_t_is_tab);
    RUN_TEST(test_escape_r_is_carriage_return);
    RUN_TEST(test_escape_unknown_emits_backslash_and_char);
    RUN_TEST(test_escape_unknown_sets_is_token);

    // Scalar name expansion.
    RUN_TEST(test_expand_scalar_name_param_zero_is_script_path);
    RUN_TEST(test_expand_scalar_name_param_one);
    RUN_TEST(test_expand_scalar_name_param_out_of_range_is_empty);
    RUN_TEST(test_expand_scalar_name_env_var);

    // Slice notation expansion.
    RUN_TEST(test_expand_slice_defaults_to_all_params);
    RUN_TEST(test_expand_slice_lhs_only);
    RUN_TEST(test_expand_slice_rhs_only);
    RUN_TEST(test_expand_slice_both_bounds);
    RUN_TEST(test_expand_slice_rhs_clamp);
    RUN_TEST(test_expand_slice_inverted_range_yields_empty);

    return UNITY_END();
}
